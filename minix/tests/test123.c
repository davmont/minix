/* Test 123 - terminal job control, on a pseudo-terminal.
 *
 * A session leader with the pty slave as its controlling terminal runs the
 * checks (the test itself feeds the master side): the foreground process
 * group and session (tcgetpgrp, tcsetpgrp, tcgetsid), background reads
 * (SIGTTIN; EIO when ignored or orphaned), background writes with TOSTOP
 * and background tcsetattr (SIGTTOU), and the INTR and SUSP characters
 * reaching the foreground process group only.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static int master;
static char slave_name[64];
static int orphan_pipe[2];		/* the orphan reports through it */

static void
watchdog(int sig)
{

	printf("timed out in subtest %d\n", subtest);
	fflush(stdout);
	_exit(1);
}

/* In the session leader: fork a child into a new process group and run
 * 'fn' in it; return its pid.
 */
static pid_t
in_group(void (*fn)(int), int fd)
{
	pid_t pid;

	if ((pid = fork()) < 0) return -1;
	if (pid == 0) {
		setpgid(0, 0);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		fn(fd);
		_exit(0);
	}
	setpgid(pid, pid);
	return pid;
}

static int
stopped_by(pid_t pid, int sig)
{
	int status;

	if (waitpid(pid, &status, WUNTRACED) != pid) return 0;
	return WIFSTOPPED(status) && WSTOPSIG(status) == sig;
}

static int
exited_with(pid_t pid, int code)
{
	int status;

	if (waitpid(pid, &status, 0) != pid) return 0;
	return WIFEXITED(status) && WEXITSTATUS(status) == code;
}

static void
reader(int fd)
{
	char c;

	/* Exit with the byte read. */
	if (read(fd, &c, 1) != 1) _exit(100 + errno);
	_exit(c);
}

static void
writer(int fd)
{

	if (write(fd, "w", 1) != 1) _exit(100 + errno);
	_exit(1);
}

static void
setter(int fd)
{
	struct termios t;

	if (tcgetattr(fd, &t) != 0) _exit(100 + errno);
	if (tcsetattr(fd, TCSANOW, &t) != 0) _exit(100 + errno);
	_exit(1);
}

static void
ignoring_reader(int fd)
{
	char c;

	signal(SIGTTIN, SIG_IGN);
	if (read(fd, &c, 1) != -1) _exit(1);
	_exit(errno == EIO ? 2 : 3);
}

static void
orphan_reader(int fd)
{
	pid_t h;
	char c;

	/* Leave a child in this group and exit: its group is orphaned (its
	 * new parent, init, is in another session).
	 */
	if ((h = fork()) < 0) _exit(1);
	if (h == 0) {
		usleep(300000);			/* the parent is gone */
		c = (read(fd, &c, 1) == -1 && errno == EIO) ? 'E' : 'x';
		(void)write(orphan_pipe[1], &c, 1);
		_exit(0);
	}
	_exit(0);
}

static void
sleeper(int fd)
{

	for (;;) pause();
}

/* The checks, in the session leader.  Returns 0 or a failure code. */
static int
leader(void)
{
	struct termios t;
	pid_t me = getpid(), c;
	int fd, status;
	char ch;

	if (setsid() != me) return 1;
	if ((fd = open(slave_name, O_RDWR)) < 0) return 2;	/* ctty */

	/* Like a shell: taking the terminal back from a job that has gone
	 * leaves us in the background, in an orphaned group (our parent is
	 * in another session), where tcsetpgrp() would fail (EIO) unless
	 * SIGTTOU is ignored.
	 */
	signal(SIGTTOU, SIG_IGN);

	/* The terminal belongs to our session, with us in front. */
	if (tcgetpgrp(fd) != me) return 3;
	if (tcgetsid(fd) != me) return 4;

	/* A background read stops on SIGTTIN; in front, it reads. */
	c = in_group(reader, fd);
	if (!stopped_by(c, SIGTTIN)) return 5;
	if (tcsetpgrp(fd, c) != 0) return 6;
	if (tcgetpgrp(fd) != c) return 7;
	kill(c, SIGCONT);
	usleep(100000);
	/* Canonical input: the line is complete with its newline. */
	if (write(master, "k\n", 2) != 2) return 8;	/* via the master */
	if (!exited_with(c, 'k')) return 9;
	if (tcsetpgrp(fd, me) != 0) return 10;

	/* Background writes are fine, unless TOSTOP. */
	c = in_group(writer, fd);
	if (!exited_with(c, 1)) return 11;
	if (tcgetattr(fd, &t) != 0) return 12;
	t.c_lflag |= TOSTOP;
	if (tcsetattr(fd, TCSANOW, &t) != 0) return 13;
	c = in_group(writer, fd);
	if (!stopped_by(c, SIGTTOU)) return 14;
	kill(c, SIGKILL);
	waitpid(c, &status, 0);
	t.c_lflag &= ~TOSTOP;
	if (tcsetattr(fd, TCSANOW, &t) != 0) return 15;

	/* Changing the settings from the background stops on SIGTTOU. */
	c = in_group(setter, fd);
	if (!stopped_by(c, SIGTTOU)) return 16;
	kill(c, SIGKILL);
	waitpid(c, &status, 0);

	/* A background read with SIGTTIN ignored fails: EIO. */
	c = in_group(ignoring_reader, fd);
	if (!exited_with(c, 2)) return 17;

	/* So does one from an orphaned process group. */
	if (pipe(orphan_pipe) != 0) return 18;
	c = in_group(orphan_reader, fd);
	if (!exited_with(c, 0)) return 18;
	if (read(orphan_pipe[0], &ch, 1) != 1 || ch != 'E') return 29;
	close(orphan_pipe[0]);
	close(orphan_pipe[1]);

	/* INTR reaches the foreground group, not the background. */
	{
		pid_t fg, bg;

		fg = in_group(sleeper, fd);
		bg = in_group(sleeper, fd);
		if (tcsetpgrp(fd, fg) != 0) return 19;
		ch = t.c_cc[VINTR];
		if (write(master, &ch, 1) != 1) return 20;
		if (waitpid(fg, &status, 0) != fg || !WIFSIGNALED(status) ||
		    WTERMSIG(status) != SIGINT) return 21;
		if (waitpid(bg, &status, WNOHANG) != 0) return 22;

		/* SUSP stops the foreground group. */
		fg = in_group(sleeper, fd);
		if (tcsetpgrp(fd, fg) != 0) return 23;
		ch = t.c_cc[VSUSP];
		if (write(master, &ch, 1) != 1) return 24;
		if (!stopped_by(fg, SIGTSTP)) return 25;
		kill(fg, SIGKILL);
		kill(bg, SIGKILL);
		waitpid(fg, &status, 0);
		waitpid(bg, &status, 0);
		if (tcsetpgrp(fd, me) != 0) return 26;
	}

	/* tcsetpgrp() errors. */
	if (tcsetpgrp(fd, 0x7ffffff0) != -1 || errno != EPERM) return 27;
	if (tcsetpgrp(fd, 1) != -1 || errno != EPERM) return 28;   /* init's */
	return 0;
}

int
main(int argc, char **argv)
{
	pid_t pid;
	int status, r, fd;
	char *name;

	start(123);
	signal(SIGALRM, watchdog);
	alarm(60);

	subtest = 1;
	if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0) e(1);
	if (grantpt(master) != 0 || unlockpt(master) != 0) e(2);
	if ((name = ptsname(master)) == NULL) e(3);
	strlcpy(slave_name, name, sizeof(slave_name));

	/* Not our controlling terminal: no foreground group to set. */
	if ((fd = open(slave_name, O_RDWR | O_NOCTTY)) < 0) e(4);
	if (tcsetpgrp(fd, getpgrp()) != -1 || errno != ENOTTY) e(5);
	close(fd);

	subtest = 2;
	if ((pid = fork()) < 0) e(1);
	if (pid == 0) {
		alarm(50);
		_exit(leader());
	}
	if (waitpid(pid, &status, 0) != pid) e(2);
	r = WIFEXITED(status) ? WEXITSTATUS(status) : 200 + WTERMSIG(status);
	if (r != 0) {
		printf("session leader: step %d failed\n", r);
		e(3);
	}
	alarm(0);
	close(master);

	quit();
	return 0;
}
