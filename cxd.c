#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
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
static int so;

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

#define DB_REMOVE_BY_PRIO 3
	/* allows us to delete entries with a priority lower than defined */
	"DELETE FROM dtab WHERE prio < ?1;",

#define DB_REMOVE_BY_ID 4
	/* allows us to delete entries from their ID */
	"DELETE FROM dtab WHERE id = ?1;",

#define DB_GET_LOOKUP_ROWS 5
	/* allows us to get all of the information needed to match a path */
	"SELECT id, path, name, prio FROM dtab ORDER BY prio DESC;",

#define DB_GET_PRIO_INFO_FROM_PATH 6
	/* allows us to get all of the information needed to recalculate this entry's priority */
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0 AND path = ?1;",

#define DB_GET_PRIO_INFO_FROM_ID 7
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0 AND id = ?1;",

#define DB_GET_ALL_PRIO_INFO 8
	/* allows us to get all of the information needed to recalculate all entries' priorities */
	"SELECT id, prio, strftime('%s', 'now', 'utc'), laccs, naccs FROM dtab WHERE locked = 0;",

#define DB_SET_PRIO_FROM_PATH 9
	/* allows us to set an entry's priority from its path */
	"UPDATE dtab SET prio = ?1 WHERE path = ?2;",

#define DB_SET_PRIO_FROM_ID 10
	/* allows us to set an entry's priority from its ID */
	"UPDATE dtab SET prio = ?1 WHERE id = ?2;",

#define DB_SET_LOCK_FROM_PATH 11
	/* allows us to set the lock of an entry from its path */
	"UPDATE dtab SET locked = ?1 WHERE path = ?2;",

#define DB_SET_LOCK_FROM_ID 12
	/* allows us to set the lock of an entry from its ID */
	"UPDATE dtab SET locked = ?1 WHERE id = ?2;",

#define DB_PATH_ACCESSED 13
	/* allows us to update the entry's information from its path */
	"UPDATE dtab SET naccs = naccs + 1, laccs = strftime('%s', 'now', 'utc') WHERE path = ?1;",

#define DB_ID_ACCESSED 14
	/* allows us to update the entry's information from its ID */
	"UPDATE dtab SET naccs = naccs + 1, laccs = strftime('%s', 'now', 'utc') WHERE id = ?1;",

#define DB_GET_MAX_ID 15
#define DB_GET_ROW_COUNT 15
	/* allows us to find out how many rows the database is holding */
	"SELECT MAX(id) FROM dtab;",

#define DB_GET_ID_FROM_PATH 16
	/* allows us to grab the ID of the entry with the given path */
	"SELECT id FROM dtab WHERE path = ?1;",

#define DB_GET_ALL_ROWS 17
	/* allows us to dump the database information */
	"SELECT * FROM dtab ORDER BY prio DESC;",

#define DB_TOGGLE_LOCK_FROM_PATH 18
	/* allows us to toggle the lock in one query using the PATH */
	"UPDATE dtab SET locked = NOT locked WHERE path = ?1;",

#define DB_TOGGLE_LOCK_FROM_ID 19
	/* allows us to toggle the lock in one query using the ID */
	"UPDATE dtab SET locked = NOT locked WHERE id = ?1;"
};

/* compiled database queries will be stored here */
static sqlite3_stmt *cdbq[sizeof(dbq)];

/* options */
static char *optstr = "dD:s:";
static char *usage = "cxd [-d] [-D dbpath] [-s socket]";
static bool daemonopt;
static bool Dflag;
static char *datapath;
static char *lockpath;
static int lockfd;
static bool sflag;
static char *socketpath;

static int show_usage(void);
void cxd_atexit(void);
static void read_sock(void);
static int get_message(int, char *);
static void hook_command(int, char *);

static void setup_socket(char *);
static void open_db(char *);
static void acquire_lockfile(char *, bool);

static int get_id_from_message(char *);
static bool is_string_numerical(char *);
static int remove_trailing_slashes(char *);

static void handle_match(int, char *);
static void handle_matchn(int, char *);
static void handle_push(int, char *);
static void handle_dump(int, char *);
static void handle_togglelock(int, char *);
static void handle_lock(int, char *);
static void handle_unlock(int, char *);
static void handle_setprio(int, char *);
static void handle_remove(int, char *);

static void recalculate_prios(void);
static int calc_prio(sqlite_int64, sqlite_int64, int);
static void find_file_regex(const regex_t *re, char *buf, size_t bufsize);
static void find_file_iter(const char *msg, int n, char *buf, size_t bufsize);
static int find_entry(char *);
static void remove_by_path(char *);
static void remove_by_id(int);
static void set_prio_from_path(char *, int *);
static void set_prio_from_id(int, int *);
static void set_lock_from_id(int, int);
static void set_lock_from_path(char *, int);

struct command {
	char *name;
	void (*func)(int cl, char *message);
};

static struct command proto_commands[] = {
	/* match a regex */
	{"MATCH", handle_match},
	/* match at most n regexes */
	{"MATCHN", handle_matchn},
	/* push directory */
	{"PUSH", handle_push},
	/* dump the array to the connection */
	{"DUMP", handle_dump},
	{"TOGGLELOCK", handle_togglelock},
	{"LOCK", handle_lock},
	{"UNLOCK", handle_unlock},
	{"SETPRIO", handle_setprio},
	{"REMOVE", handle_remove},
	/* null entry to know when to stop */
	{NULL, NULL}
};

int
main(int argc, char **argv)
{
	int c;
	char buf[PATH_MAX + 1];
	memset(buf, 0, sizeof(buf));

	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch(c) {
		case 'd':
			daemonopt = true;
			break;
		case 'D':
			Dflag = true;
			datapath = optarg;
			break;
		case 's':
			sflag = true;
			socketpath = optarg;
			break;
		default:
			return show_usage();
		}
	}

	/* daemonize if asked */
	if (daemonopt && fork())
		return 0;

	atexit(cxd_atexit);

	/* check whether the path defined in macros exists */
	cx_set_path("", false, buf, sizeof(buf));
	if (access(buf, X_OK) != 0)
		/* it doesn't exist, create it */
		mkdir(buf, 0755);

	lockpath = DEFAULT_LOCKFILE_NAME;
	cx_set_path(lockpath, false, buf, sizeof(buf));
	lockpath = buf;
	acquire_lockfile(lockpath, !daemonopt);

	if (socketpath == NULL)
		socketpath = DEFAULT_SOCKET_NAME;
	cx_set_path(socketpath, sflag, buf, sizeof(buf));
	socketpath = buf;
	setup_socket(socketpath);

	if (datapath == NULL)
		datapath = DEFAULT_DATAFILE_NAME;
	cx_set_path(datapath, Dflag, buf, sizeof(buf));
	datapath = buf;
	open_db(datapath);

	read_sock();

	return 0;
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
acquire_lockfile(char *path, bool verbose)
{
	lockfd = open(path, O_RDWR | O_CREAT);
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
		if (verbose)
			fputs("failed to acquire lockfile. is cxd already running?", stderr);
		exit(1);
	}
	return;
}

static void
open_db(char *path)
{
	sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

	int i;
	const char *p;
	/* compile the queries */
	for (i = 0; i < (sizeof(dbq)/8); i++) {
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
setup_socket(char *path)
{
	struct sockaddr_un sun;
	size_t size;

	/* eventually this should be done when we're closing */
	unlink(path);
	so = socket(AF_UNIX, SOCK_STREAM, 0);
	if (so < 0)
		err(errno, "failed to create socket");

	memset(sun.sun_path, 0, sizeof(sun.sun_path));
	sun.sun_family = AF_UNIX;
	if (strlen(path) > sizeof(sun.sun_path)) {
		err(1, "Path too long");
	} else {
		strncpy(sun.sun_path, path, sizeof(sun.sun_path));
	}

	size = sizeof(struct sockaddr_un);

	if (bind(so, (struct sockaddr *)&sun, size) == -1)
		err(errno, "failed to bind socket to \"%s\"", path);
	if (listen(so, 5) == -1)
		err(errno, "failed to listen to socket");

	return;
}

static void
read_sock()
{
	char buf[PATH_MAX + 1];
	int ret;
	int cl = 0;
	bool connected = false;

	while (connected || ((cl = accept(so, NULL, NULL)) != -1 && (connected = true))) {
		memset(buf, 0, sizeof(buf));
		ret = get_message(cl, buf);
		if (ret || buf[0] == '\0') {
			close(cl);
			connected = false;
			continue;
		}

		hook_command(cl, buf);
	}

	err(errno, "failed to read from socket");

	/* NOT REACHED */
	return;
}

static int
get_message(int cl, char *buf)
{
	int i;
	int ret;
	for (i = 0; (ret = read(cl, buf + i, 1)) != -1 && buf[i] != '\0' && buf[i] != EOF; i++) {
		if (i == PATH_MAX) {
			/* message too long */
			return 1;
		}
	}
	buf[i] = '\0';

	if (ret == -1 || buf[i] == EOF)
		return 1;

	return 0;
}

static void
hook_command(int cl, char *message)
{
	struct command cmd;
	int cmdlen;

	int i;
	for (i = 0; proto_commands[i].name != NULL; i++) {
		cmd = proto_commands[i];
		cmdlen = strlen(cmd.name);

		if (strncmp(message, cmd.name, cmdlen) == 0 &&
		    (message[cmdlen] == '\0' || message[cmdlen] == ' ')) {
			/* strip the command */
			message += cmdlen;
			while (*message == ' ')
				message++;

			/* call the function acting on it */
			cmd.func(cl, message);
			break;
		}
	}

	return;
}

static void
handle_match(int cl, char *message)
{
	int ret;
	char buf[4096];
	memset(buf, 0, sizeof(buf));

	/* make sure equivalent paths can compare */
	remove_trailing_slashes(message);
	/* make sure our information is accurate */
	recalculate_prios();

	/* compile the regex */
	regex_t re;
	ret = regcomp(&re, message, REG_ICASE | REG_NOSUB);
	if (ret)		
		find_file_regex(&re, buf, sizeof(buf));
	else
		find_file_iter(message, strlen(message)/2, buf, sizeof(buf));
	
	write(cl, buf, strlen(buf) + 1);
	
	regfree(&re);
	return;
}

static void
handle_matchn(int cl, char *message)
{
	long int i, n;
	char *path, *name;
	regex_t re;
	for (i = 0; isdigit(message[i]); i++)
		;

	if (i == 0) {
		write(cl, "\0", 1);
		return;
	}

	remove_trailing_slashes(message);
	recalculate_prios();

	char *digits_end = message + i++;
	while (message[i] == ' ' && message[i] != '\0')
		;
	char *msg_start = message + i;
	regcomp(&re, msg_start, REG_ICASE | REG_NOSUB);


	i = 0;
	n = strtol(message, &digits_end, 10);
	while (sqlite3_step(cdbq[DB_GET_LOOKUP_ROWS]) == SQLITE_ROW && (n == -1 || i < n)) {
		path = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 1);
		name = (char *)sqlite3_column_text(cdbq[DB_GET_LOOKUP_ROWS], 2);

		if (access(path, X_OK) != 0) {
			/* that path is gone, remove it and keep going */
			remove_by_path(path);
			continue;
		}

		if (regexec(&re, name, 0, NULL, 0) == 0) {
			write(cl, path, strlen(path));
			write(cl, "\n", 1);
			i++;
		}
	}

	write(cl, "\0", 1);
	sqlite3_reset(cdbq[DB_GET_LOOKUP_ROWS]);
	regfree(&re);
}

static void
handle_push(int cl, char *message)
{
	sqlite3_stmt *stmt;
	int i, j;
	int id;
	char name[256];
	memset(name, 0, sizeof(name));

	int msglen = remove_trailing_slashes(message);

	if (msglen > PATH_MAX || access(message, X_OK) != 0)
		return;

	id = find_entry(message);
	if (id >= 0) {
		/* the entry exists, tell the database to update it */
		stmt = cdbq[DB_ID_ACCESSED];
		sqlite3_bind_int(stmt, 1, id);
	} else {
		/* the entry doesn't exist, parse the name from the path and add to db */
		j = 0;
		for (i = 0; message[i] != '\0' && j < sizeof(name) - 1; i++) {
			if (message[i] == '/')
				j = 0;
			else
				name[j++] = message[i];
		}
		name[j] = '\0';

		/* now that we've got the name, insert the new entry to sqlite */
		stmt = cdbq[DB_INSERT];
		sqlite3_bind_text(stmt, 1, message, strlen(message), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, name, strlen(name), SQLITE_STATIC);
	}

	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	set_prio_from_path(message, NULL);
	return;
}

static void
handle_dump(int cl, char *message)
{
	char buf[8096];
	sqlite3_stmt *stmt = cdbq[DB_GET_ALL_ROWS];

	/* make sure our information is up to date */
	recalculate_prios();

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
		write(cl, buf, strlen(buf));
	}

	sqlite3_reset(stmt);
	write(cl, "\0", 1);
	return;
}

static void
handle_togglelock(int cl, char *message)
{
	sqlite3_stmt *stmt;
	int id;
	if (is_string_numerical(message)) {
		id = get_id_from_message(message);
		stmt = cdbq[DB_TOGGLE_LOCK_FROM_ID];
		sqlite3_bind_int(stmt, 1, id);
	} else {
		stmt = cdbq[DB_TOGGLE_LOCK_FROM_PATH];
		sqlite3_bind_text(stmt, 1, message, strlen(message), SQLITE_STATIC);
	}

	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return;
}

static void
handle_lock(int cl, char *message)
{
	int id;
	if (is_string_numerical(message)) {
		id = get_id_from_message(message);
		set_lock_from_id(id, 1);
	} else {
		set_lock_from_path(message, 1);
	}

	return;	
}

static void
handle_unlock(int cl, char *message)
{
	int id;
	if (is_string_numerical(message)) {
		id = get_id_from_message(message);
		set_lock_from_id(id, 0);
	} else {
		set_lock_from_path(message, 0);
	}

	return;	
}

static void
handle_setprio(int cl, char *message)
{
	errno = 0;
	int msglen = strlen(message);
	char *idend = index(message, ' ');
	char *msgend = message + msglen;

	int id = strtol(message, &idend, 0);
	int prio = strtol(message + (int)(msgend - (idend + 1)), &msgend, 0);
	if (errno == 0) {
		set_lock_from_id(id, 1);
		set_prio_from_id(id, &prio);
	} else {
		errno = 0;
	}

	return;
}

static void
handle_remove(int cl, char *message)
{
	int id;
	if (is_string_numerical(message)) {
		id = get_id_from_message(message);
		remove_by_id(id);
	} else {
		remove_by_path(message);
	}

	return;
}

static int
remove_trailing_slashes(char *str)
{
	int i;
	size_t len = strlen(str);
	for (i = len - 1; str[i] == '/' && i > 0; i--)
		str[i] = '\0';

	return i + 1;
}

static bool
is_string_numerical(char *str)
{
	int i, strsize = strlen(str);
	for (i = 0; i < strsize; i++)
		if (!isdigit(str[i]))
			return false;

	return true;
}

static int
get_id_from_message(char *message)
{
	errno = 0;

	char *end = index(message, ' ');
	int msglen = strlen(message);
	if (end == NULL)
		end = message + msglen;

	int id = strtol(message, &end, 0);
	if (errno == 0) {
		return id;
	} else {
		errno = 0;
		return -1;
	}
}

static void
recalculate_prios()
{
	sqlite3_stmt *stmt = cdbq[DB_GET_ALL_PRIO_INFO];
	sqlite3_int64 now, laccs;
	int naccs, prio, id;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		id = sqlite3_column_int(stmt, 0);
		now = sqlite3_column_int64(stmt, 2);
		laccs = sqlite3_column_int64(stmt, 3);
		naccs = sqlite3_column_int(stmt, 4);
		prio = calc_prio(now, laccs, naccs);
		set_prio_from_id(id, &prio);
	}

	sqlite3_reset(stmt);
	return;
}

static int
calc_prio(sqlite_int64 now, sqlite_int64 laccs, int naccs)
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
			strncpy(buf, path, bufsize);
			strcat(buf, "\n");
			break;
		}
	}

	sqlite3_reset(cdbq[DB_GET_LOOKUP_ROWS]);
}

static void
find_file_iter (const char *msg, int n, char *buf, size_t bufsize)
{
	char *path, *name;
	size_t i, len, bm;

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
			if (name[i] == msg[i])
				bm++;
			else
				bm = 0;

			if (bm > n) {
				strncpy(buf, path, bufsize);
				strcat(buf, "\n");
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
	return;
}

static void
remove_by_id(int id)
{
	sqlite3_stmt *stmt = cdbq[DB_REMOVE_BY_ID];
	sqlite3_bind_int(stmt, 1, id);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return;
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
	return;
}

static void
set_lock_from_path(char *path, int lockval)
{
	sqlite3_stmt *stmt = cdbq[DB_SET_LOCK_FROM_PATH];
	sqlite3_bind_int(stmt, 1, lockval);
	sqlite3_bind_text(stmt, 2, path, strlen(path), SQLITE_STATIC);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return;
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
		nprio = calc_prio(now, laccs, naccs);
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
	return;
}

static void
set_prio_from_path(char *path, int *prio)
{
	int nprio;
	sqlite3_stmt *stmt;
	int naccs;
	sqlite3_int64 now, laccs;

	if (prio == NULL) {
		stmt = cdbq[DB_GET_PRIO_INFO_FROM_PATH];
		sqlite3_bind_text(stmt, 1, path, strlen(path), SQLITE_STATIC);
		if (sqlite3_step(stmt) != SQLITE_ROW)
			return;

		now = sqlite3_column_int64(stmt, 2);
		naccs = sqlite3_column_int64(stmt, 3);
		laccs = sqlite3_column_int(stmt, 4);
		nprio = calc_prio(now, laccs, naccs);
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	} else {
		nprio = *prio;
	}

	stmt = cdbq[DB_SET_PRIO_FROM_PATH];
	sqlite3_bind_int(stmt, 1, nprio);
	sqlite3_bind_text(stmt, 2, path, strlen(path), SQLITE_STATIC);
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return;
}
