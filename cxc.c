#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h> // locks
#include <getopt.h>
#include <regex.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cx.h"

#define MAXMSGLEN (PATH_MAX + 512)

static sqlite3 *db;

/*
 * Database queries to compile for later use during init.
 * They will be placed in cdbq, where the indexes will stay true.
 */
static char *dbq[] = {
#define DB_TAB_CREATE 0
	/*
	 * allows us to initialize the database
	 * table information:
	 * id: the unique entry's ID
	 * path: the entry's filesystem path
	 * name: the name of the directory, without the rest of the path
	 * prio: the entry's priority
	 * locked: whether the priority is locked
	 * laccs: last access time as a unix timestamp in seconds
	 * naccs: the amount of times the directory was accessed
	 */
	"CREATE TABLE IF NOT EXISTS dtab (id INTEGER UNIQUE PRIMARY KEY, path VARCHAR(1024) UNIQUE, name VARCHAR(256), prio INTEGER, locked INTEGER, laccs INTEGER, naccs INTEGER);",

#define DB_INSERT 1
	/* allows us to insert new entries, only requires to set the path and name */
	"INSERT INTO dtab SELECT MAX(id) + 1, ?1, ?2, 0, 0, strftime('%s', 'now', 'utc'), 1 FROM dtab;",

#define DB_REMOVE_BY_PATH 2
	/* allows us to delete entries matching the path */
	"DELETE FROM dtab WHERE path = ?1;",

#define DB_REMOVE_BY_ID 3
	/* allows us to delete entries from their ID */
	"DELETE FROM dtab WHERE id = ?1;",

#define DB_GET_LOOKUP_ROWS 4
	/* allows us to get all of the information needed to match a path */
	"SELECT id, path, name, prio FROM dtab ORDER BY prio DESC;",

#define DB_GET_PRIO_INFO_FROM_PATH 5
	/* allows us to get all of the information needed to recalculate this entry's priority */
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0 AND path = ?1;",

#define DB_GET_PRIO_INFO_FROM_ID 6
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0 AND id = ?1;",

#define DB_GET_ALL_PRIO_INFO 7
	/* allows us to get all of the information needed to recalculate all entries' priorities */
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0;",

#define DB_SET_PRIO_FROM_ID 8
	/* allows us to set an entry's priority from its ID */
	"UPDATE dtab SET prio = ?1 WHERE id = ?2;",

#define DB_SET_LOCK_FROM_ID 9
	/* allows us to set the lock of an entry from its ID */
	"UPDATE dtab SET locked = ?1 WHERE id = ?2;",

#define DB_ID_ACCESSED 10
	/* allows us to update the entry's information from its ID */
	"UPDATE dtab SET naccs = naccs + 1, laccs = strftime('%s', 'now', 'utc') WHERE id = ?1;",

#define DB_GET_ID_FROM_PATH 11
	/* allows us to grab the ID of the entry with the given path */
	"SELECT id FROM dtab WHERE path = ?1;",

#define DB_GET_ALL_ROWS 12
	/* allows us to dump the database information */
	"SELECT * FROM dtab ORDER BY prio DESC;",

#define DB_TOGGLE_LOCK_FROM_ID 13
	/* allows us to toggle the lock in one query using the ID */
	"UPDATE dtab SET locked = NOT locked WHERE id = ?1;"
};

/* compiled database queries will be stored here */
static sqlite3_stmt *cdbq[sizeof(dbq)];

/* options */
static char *optstr = "IdlrtuD:s:i:n:p:";
static char usage[] = "cx [-Idlrtu] [-D dbpath] [-S <prio>] [-i <id>] [-n match_count] [-p <path>] [--] <string to match>";
static bool dflag, Dflag, Iflag, lflag, rflag, tflag, uflag, sflag, pflag;
static char *pushpath;
static char *datapath;
static char *lockpath;
static int lockfd, prio, id = -1;

void cxd_atexit(void);

static int show_usage(void);

static void set_path(char *, bool, char *, size_t);
static void open_db(char *);
static void acquire_lockfile(char *);

static void get_match(char *to_match, char *buf, size_t bufsize);
static int push_path(char *str);
static void write_dump();
static void set_locked();
static void set_unlocked();
static void toggle_lock();
static void set_priority();

static void recalculate_priorities(void);
static int calculate_priority(sqlite_int64, sqlite_int64, int);
static void find_file_regex(const regex_t *re, char *buf, size_t bufsize);
static void find_file_iter(char *buf, size_t bufsize);
static int find_entry(char *);
static void remove_by_path(char *);
static void remove_by_id(int);
static void set_prio_from_id(int, int *);
static void set_lock_from_id(int, int);

struct command {
	char *name;
	void (*func)(char *message);
};

int
main(int argc, char **argv)
{
	int c;
	char buf[PATH_MAX + 1];
	memset(buf, 0, sizeof(buf));
	char match[PATH_MAX + 1000];
	memset(match, 0, sizeof(match));
	if (argc == 1)
		return show_usage();

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch(c) {
		case 'd':
			dflag = true;
			break;
		case 'l':
			lflag = true;
			break;
		case 'r':
			rflag = true;
			break;
		case 't':
			tflag = true;
			break;
		case 'u':
			uflag = true;
			break;
		case 'D':
			Dflag = true;
			datapath = optarg;
			break;
		case 'I':
			Iflag = true;
			break;
		case 'S':
			sflag = true;
			prio = atoi(optarg);
			if (errno)
				err(errno, "failed to parse requested priority");
			break;
		case 'i':
			id = atoi(optarg);
			if (errno)
				err(errno, "failed to parse requested id");
			break;
		case 'p':
			pflag = true;
			pushpath = optarg;
			break;
		default:
		        break;
		}
	}

	atexit(cxd_atexit);

	/* check whether the path defined in macros exists */
	set_path("", false, buf, sizeof(buf));
	if (access(buf, X_OK) != 0)
		/* it doesn't exist, create it */
		mkdir(buf, 0755);

	lockpath = DEFAULT_LOCKFILE_NAME;
	set_path(lockpath, false, buf, sizeof(buf));
	lockpath = buf;
	acquire_lockfile(lockpath);

	if (datapath == NULL)
		datapath = DEFAULT_DATAFILE_NAME;
	set_path(datapath, Dflag, buf, sizeof(buf));
	datapath = buf;
	open_db(datapath);

	if (dflag) {
		write_dump();
		return 0;
	}

	if (pflag) {
		if (fork()) // Fork to avoid waiting
			return 0;
		else
			return push_path(pushpath);
	}

	if (id == -1) {
		get_match(argv[argc - 1], match, sizeof(match));
		if (match[0] == '\0') {
			write(2, "No matching entry.\n", 18);
			write(1, ".\n", 2);
		} else {
			id = find_entry(match);
			if (id == -1)
				err(1, "No match found");

			if (Iflag) {
				printf("%s: %d\n", match, id);
			} else {
				puts(match);
			}
		}
	}

	if (lflag)
		set_locked();
	if (tflag)
		toggle_lock();
	if (uflag)
		set_unlocked();
	if (rflag)
		remove_by_id(id);
	if (sflag)
		set_priority();
}

void
cxd_atexit()
{
	sqlite3_close_v2(db);
	fcntl(lockfd, F_UNLCK, NULL);
}

static int
show_usage()
{
	write(1, usage, sizeof(usage));
	write(1, "\n", 1);
	return 2;
}

static void
acquire_lockfile(char *path)
{
	lockfd = open(path, O_RDWR | O_CREAT, 0644);
	chmod(path, S_IWUSR | S_IRUSR);
	if (lockfd < 0)
		err(errno, "failed to open the lock file");

	pid_t pid = getpid();
	struct flock fl;
	memset(&fl, 0, sizeof(fl));

	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = pid;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;

	if (fcntl(lockfd, F_SETLK, &fl) == -1) {
	        fputs("Failed to acquire lockfile.", stderr);
		exit(1);
	}
	return;
}

static void
set_path(char *path, bool absolute, char *buf, size_t buflen)
{
	char ibuf[buflen];
	memset(ibuf, 0, buflen);
	memset(buf, 0, buflen);

#ifdef CX_DATA_DIR
	const char *data_path = CX_DATA_DIR;
#else
	const char *data_path = getenv("XDG_DATA_HOME");
	if (data_path == NULL)
		data_path = "~/.local/share";
#endif

	if (absolute)
		strncpy(ibuf, path, buflen - 1);
	else
		snprintf(ibuf, buflen, "%s/%s/%s", data_path, CX_DIR_NAME, path);

	if (ibuf[0] == '~')
		snprintf(buf, buflen, "%s%s", getenv("HOME"), ibuf + 1);
	else
		strncpy(buf, ibuf, buflen);

	buf[buflen - 1] = '\0';
	return;
}


static void
open_db(char *path)
{
	sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

	const char *p;
	/* compile the queries */
	for (unsigned i = 0; i < sizeof(dbq)/8; i++) {
#if SQLITE_VERSION_NUMBER >= 3020000
		sqlite3_prepare_v3(db, dbq[i], strlen(dbq[i]), SQLITE_PREPARE_PERSISTENT, &cdbq[i], &p);
#else
		sqlite3_prepare_v2(db, dbq[i], strlen(dbq[i]), &cdbq[i], &p);
#endif

		/* make sure the table exists */
		if (i == 0) {
			sqlite3_step(cdbq[DB_TAB_CREATE]);
			sqlite3_reset(cdbq[DB_TAB_CREATE]);
		}
	}

	return;
}

static void
get_match(char *to_match, char *buf, size_t bufsize)
{
	if (to_match == NULL)
		return;

	int ret;
	memset(buf, 0, bufsize);
	strcpy(buf, to_match);

	/* make sure our information is accurate */
	recalculate_priorities();

	/* compile the regex */
	regex_t re;
	ret = regcomp(&re, buf, REG_ICASE | REG_NOSUB);
	if (ret)		
		find_file_regex(&re, buf, bufsize);
	else
		find_file_iter(buf, bufsize); // TODO: Replace this

	regfree(&re);
}

static int
push_path(char *path)
{
	sqlite3_stmt *stmt;
	unsigned i, j;
	char name[256];
	memset(name, 0, sizeof(name));

	if (access(path, X_OK) != 0)
		return 1;

	id = find_entry(path);
	if (id >= 0) {
		/* the entry exists, tell the database to update it */
		stmt = cdbq[DB_ID_ACCESSED];
		sqlite3_bind_int(stmt, 1, id);
	} else {
		/* the entry doesn't exist, parse the name from the path and add to db */
		j = 0;
		for (i = 0; path[i] != '\0' && j < sizeof(name) - 1; i++) {
			if (path[i] == '/')
				j = 0;
			else
				name[j++] = path[i];
		}
		name[j] = '\0';

		/* now that we've got the name, insert the new entry to sqlite */
		stmt = cdbq[DB_INSERT];
		sqlite3_bind_text(stmt, 1, path, strlen(path), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, name, strlen(name), SQLITE_STATIC);
	}

	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	id = find_entry(path);
	if (id != -1)
		set_prio_from_id(id, NULL);
	else
		return 1;

	return 0;
}

static void
write_dump()
{
	char buf[8192];
	sqlite3_stmt *stmt = cdbq[DB_GET_ALL_ROWS];

	/* make sure our information is up to date */
	recalculate_priorities();

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		snprintf(buf, 8096, "id: %d | path: %s | name: %s |"
		    " priority: %u | priority locked: %d | last access: %lld | access count: %d\n",
		    sqlite3_column_int(stmt, 0),	/* id */
		    sqlite3_column_text(stmt, 1),	/* path */
		    sqlite3_column_text(stmt, 2),	/* name */
		    sqlite3_column_int(stmt, 3),	/* prio */
		    sqlite3_column_int(stmt, 4),	/* locked */
		    sqlite3_column_int64(stmt, 5),	/* laccs */
		    sqlite3_column_int(stmt, 6));	/* naccs */
		write(1, buf, strlen(buf));
	}

	sqlite3_reset(stmt);
}

static void
set_locked()
{
	if (id == -1)
		err(1, "no id");

        set_lock_from_id(id, 1);
}

static void
set_unlocked()
{
	if (id == -1)
		err(1, "no id");

        set_lock_from_id(id, 0);
}

static void
toggle_lock()
{
	if (id == -1)
		err(1, "no id");

	sqlite3_stmt *stmt;
	stmt = cdbq[DB_TOGGLE_LOCK_FROM_ID];
	sqlite3_bind_int(stmt, 1, id);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}

static void
set_priority()
{
	if (id == -1)
		err(1, "no id");

	char buf[256];
	snprintf(buf, 256, "%d %d", id, prio);
	char *end = buf + strlen(buf);
	int prio = strtol(buf, &end, 0);
	if (errno == 0) {
		set_lock_from_id(id, 1);
		set_prio_from_id(id, &prio);
	} else {
		err(errno, "Failed to set priority");
	}
}

static void
recalculate_priorities()
{
	sqlite3_stmt *stmt = cdbq[DB_GET_ALL_PRIO_INFO];
	sqlite3_int64 now, laccs;
	int naccs, prio, id;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		id = sqlite3_column_int(stmt, 0);
		now = sqlite3_column_int64(stmt, 2);
		laccs = sqlite3_column_int64(stmt, 3);
		naccs = sqlite3_column_int(stmt, 4);
		prio = calculate_priority(now, laccs, naccs);
		set_prio_from_id(id, &prio);
	}

	sqlite3_reset(stmt);
}

static int
calculate_priority(sqlite_int64 now, sqlite_int64 laccs, int naccs)
{
	int dt = now - laccs;
	int a = 2;
	int x;

	if (dt < 600)
		x = (double)naccs * 2 * a;
	else if (dt >= 600 && dt < 3600)
		x = (double)naccs * a;
	else if (dt >= 3600)
		x = (double)naccs / a;
	else
		x = (double)naccs / (2 * a);

	return x;
}

static void
find_file_regex(const regex_t *re, char *buf, size_t bufsize)
{
	char *path, *name;

	while(sqlite3_step(cdbq[DB_GET_LOOKUP_ROWS]) == SQLITE_ROW) {
		path = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 1);
		name = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 2);

		if (access(path, X_OK) != 0) {
			/* that path is gone, remove it and keep going */
			remove_by_path(path);
			continue;
		}
		
		if (regexec(re, name, 0, NULL, 0) == 0) {
			strncpy(buf, path, bufsize - 1);
			break;
		}
	}

	sqlite3_reset(cdbq[DB_GET_LOOKUP_ROWS]);
}

static void
find_file_iter(char *buf, size_t bufsize)
{
	char *path, *name;
	size_t i, len, bm, n = strlen(buf)/2;

	while(sqlite3_step(cdbq[DB_GET_LOOKUP_ROWS]) == SQLITE_ROW) {
		path = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 1);
		name = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 2);
		bm = 0;
		len = strlen(name);

		if (access(path, X_OK) != 0) {
			/* that path is gone, remove it and keep going */
			remove_by_path(path);
			continue;
		}

    		for (i = 0; i < len; i++) {
			if (name[i] == buf[i])
				bm++;
			else
				bm = 0;

			if (bm > n) {
				strncpy(buf, path, bufsize - 1);
				break;
			}
		}
	}

	sqlite3_reset(cdbq[DB_GET_LOOKUP_ROWS]);
}

static int
find_entry(char *path)
{
	int id, ret;
	sqlite3_stmt *stmt = cdbq[DB_GET_ID_FROM_PATH];
	sqlite3_bind_text(stmt, 1, path, strlen(path), SQLITE_STATIC);
	ret = sqlite3_step(stmt);
	if (ret == SQLITE_ROW)
		id = sqlite3_column_int(stmt, 0);
	else
		id = -1;

	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return id;
}

static void
remove_by_path(char *path)
{
	sqlite3_stmt *stmt = cdbq[DB_REMOVE_BY_PATH];
	sqlite3_bind_text(stmt, 1, path, strlen(path), SQLITE_STATIC);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}

static void
remove_by_id(int id)
{
	sqlite3_stmt *stmt = cdbq[DB_REMOVE_BY_ID];
	sqlite3_bind_int(stmt, 1, id);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}

static void
set_lock_from_id(int id, int lockval)
{
	sqlite3_stmt *stmt = cdbq[DB_SET_LOCK_FROM_ID];
	sqlite3_bind_int(stmt, 1, lockval);
	sqlite3_bind_int(stmt, 2, id);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}

static void
set_prio_from_id(int id, int *prio)
{
	int nprio;
	sqlite3_stmt *stmt;
	int naccs;
	sqlite3_int64 now, laccs;

	if (prio == NULL) {
		stmt = cdbq[DB_GET_PRIO_INFO_FROM_ID];
		sqlite3_bind_int(stmt, 1, id);
		if (sqlite3_step(stmt) != SQLITE_ROW)
			return;

		now = sqlite3_column_int64(stmt, 2);
		naccs = sqlite3_column_int64(stmt, 3);
		laccs = sqlite3_column_int(stmt, 4);
		nprio = calculate_priority(now, laccs, naccs);
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	} else {
		nprio = *prio;
	}

	stmt = cdbq[DB_SET_PRIO_FROM_ID];
	sqlite3_bind_int(stmt, 1, nprio);
	sqlite3_bind_int(stmt, 2, id);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}
