#include <sys/socket.h>
#include <sys/param.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cx.h"

static char optstr[] = "-dlurtS:i:s:p:";
static char usage[] = "cxc [-dlurt] [-S <prio>] [-i <id>] [-p <path>] [-s <socket path>] [--] <string to match>";
static bool dflag;
static bool lflag;
static bool uflag;
static bool tflag;
static bool Sflag;
static bool sflag;
static bool pflag;
static int prio;
static int id = -1;
static char *pushpath;
static char *socketpath;

static int show_usage(void);
static int setup_connection(char *path);
static char *get_match(int so, char *to_match, char *buf, size_t bufsize);
static void push_path(int so, char *str);
static void write_dump(int so);
static void set_locked(int);
static void set_unlocked(int);
static void toggle_lock(int);
static void set_prio(int);

int
main(int argc, char *argv[])
{
	char buf[PATH_MAX + 1];
	memset(buf, 0, sizeof(buf));
	char match[PATH_MAX + 1];
	memset(match, 0, sizeof(match));
	if (argc == 1)
		return show_usage();

	char c;
	bool should_match = true;
	while ((c = getopt(argc, argv, optstr)) != -1) {
		should_match = false;
		switch(c) {
		case 'd':
			dflag = true;
			break;
		case 'l':
			lflag = true;
			break;
		case 'u':
			uflag = true;
			break;
		case 't':
			tflag = true;
			break;
		case 'S':
			Sflag = true;
			prio = atoi(optarg);
			if (errno)
				err(errno, "failed to parse requested priority");
			break;
		case 'i':
			id = atoi(optarg);
			if (errno)
				err(errno, "failed to parse requested id");
			break;
		case 's':
			should_match = true;
			sflag = true;
			socketpath = optarg;
			break;
		case 'p':
			pflag = true;
			pushpath = optarg;
			break;
		case '-':
			goto opt_loop_end;
		default:
			break;
			
		}
	}
opt_loop_end:

	if (!sflag)
		socketpath = DEFAULT_SOCKET_NAME;

	cx_set_path(socketpath, sflag, buf, sizeof(buf));
	socketpath = buf;

	int so = setup_connection(socketpath);
	if (so == -1)
		err(errno, "failed to setup the socket");

	if (lflag)
		set_locked(so);

	if (uflag)
		set_unlocked(so);

	if (tflag)
		toggle_lock(so);

	if (Sflag)
		set_prio(so);

	if (dflag)
		write_dump(so);
	if (pflag)
		push_path(so, pushpath);

	if (should_match) {
		get_match(so, argv[argc - 1], match, PATH_MAX + 1);
		if (match[0] == '\0') {
			write(2, "No matching entry\n", 18);
			write(1, ".\n", 2);
		} else {
			write(1, match, strlen(match));
		}
	}

	close(so);
	return 0;
}

static int
show_usage()
{
	write(2, usage, sizeof(usage));
	write(2, "\n", 1);
	return 2;
}

static int
setup_connection(char *path)
{
	struct sockaddr_un sun;
	memset(&sun, 0, sizeof(sun));

	int so = socket(AF_UNIX, SOCK_STREAM, 0);
	if (strlen(path) > sizeof(sun.sun_path)) {
		err(1, "Path too long");
	} else {
		strncpy(sun.sun_path, path, sizeof(sun.sun_path));
	}
	sun.sun_family = AF_UNIX;

	int ret = connect(so, (struct sockaddr *)&sun, sizeof(sun));
	if (ret == -1)
		err(1, "Couldn't connect to the socket at %s. Is cxd running?", path);

	return so;
}

static void
push_path(int so, char *str)
{
	char message[PATH_MAX + 256];
	snprintf(message, PATH_MAX + 256, "PUSH %s", str);
	int ret = write(so, message, strlen(message));
	if (ret == -1)
		err(errno, "Failed to write to the socket. Is cxd running?");

	return;
}

static char *
get_match(int so, char *to_match, char *buf, size_t bufsize)
{
	if (to_match == NULL)
		return NULL;

	char message[PATH_MAX + 256];
	memset(message, 0, sizeof(message));
	memset(buf, 0, bufsize);

	snprintf(message, sizeof(message), "MATCH %s", to_match);
	write(so, message, strlen(message) + 1);

	int ret, i = 0;
	while (i < bufsize && (ret = read(so, buf + i, 1)) != -1 && buf[i] != '\0')
		i++;

	buf[i] = '\0';
	return buf;
}

static void
write_dump(int so)
{
	char message[] = "DUMP";
	write(so, message, sizeof(message));

	char c;
	while (read(so, &c, 1) != -1 && c != '\0')
		write(1, &c, 1);

	return;
}

static void
set_locked(int so)
{
	if (id == -1)
		err(1, "-l requires -i");

	char buf[256];
	snprintf(buf, 256, "LOCK %d", id);
	write(so, buf, strlen(buf));
	return;
}

static void
set_unlocked(int so)
{
	if (id == -1)
		err(1, "-u requires -i");

	char buf[256];
	snprintf(buf, 256, "UNLOCK %d", id);
	write(so, buf, strlen(buf));
	return;
}

static void
toggle_lock(int so)
{
	if (id == -1)
		err(1, "-t requires -i");

	char buf[256];
	snprintf(buf, 256, "TOGGLELOCK %d", id);
	write(so, buf, strlen(buf));
	return;
}

static void
set_prio(int so)
{
	if (id == -1)
		err(1, "-S requires -i");

	char buf[256];
	snprintf(buf, 256, "SETPRIO %d %d", id, prio);
	write(so, buf, strlen(buf));
	return;
}
