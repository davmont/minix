/* Test 124 - job control in the shell, end to end.
 *
 * An interactive /bin/sh runs on a pseudo-terminal (as a session leader with
 * the pty as its controlling terminal); this test types at it through the
 * master side: ^Z stops a foreground job, jobs/bg/fg move it around, ^C ends
 * it, and a background job reading the terminal is stopped (SIGTTIN).
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define PROMPT	"JCPROMPT> "

static int master;
static char out[16384];		/* what the shell printed since the last mark */
static size_t outlen;

static void
watchdog(int sig)
{

	printf("timed out in subtest %d; shell output:\n%.*s\n", subtest,
	    (int)outlen, out);
	fflush(stdout);
	_exit(1);
}

/* Collect the shell's output until 'want' shows up after 'n' prompts, or
 * 'ms' go by.  Returns whether it showed up.
 */
static int
expect(const char *want, int prompts, int ms)
{
	struct pollfd pfd;
	ssize_t r;
	int waited = 0;
	char *p;
	int n;

	for (;;) {
		out[outlen] = '\0';
		if (want == NULL || strstr(out, want) != NULL) {
			/* Count the prompts printed so far. */
			for (n = 0, p = out; (p = strstr(p, PROMPT)) != NULL;
			    p += sizeof(PROMPT) - 1)
				n++;
			if (n >= prompts)
				return 1;
		}
		if (waited >= ms) {
			printf("subtest %d: no \"%s\" (%d prompts) in:\n%s\n",
			    subtest, want ? want : "", prompts, out);
			fflush(stdout);
			return 0;
		}
		pfd.fd = master;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 100) > 0 && outlen < sizeof(out) - 1) {
			r = read(master, out + outlen, sizeof(out) - 1 - outlen);
			if (r > 0) outlen += r;
		} else
			waited += 100;
	}
}

static void
type(const char *s)
{

	outlen = 0;			/* look at new output only */
	if (write(master, s, strlen(s)) != (ssize_t)strlen(s)) e(90);
}

static void
key(char c)
{

	outlen = 0;
	if (write(master, &c, 1) != 1) e(91);
}

int
main(int argc, char **argv)
{
	struct termios t;
	pid_t sh;
	char *name;
	int status, fd;

	start(124);
	signal(SIGALRM, watchdog);
	alarm(90);

	subtest = 1;
	if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0) e(1);
	if (grantpt(master) != 0 || unlockpt(master) != 0) e(2);
	if ((name = ptsname(master)) == NULL) e(3);

	if ((sh = fork()) < 0) e(4);
	if (sh == 0) {
		setsid();
		if ((fd = open(name, O_RDWR)) < 0) _exit(1);	/* ctty */
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if (fd > 2) close(fd);
		close(master);
		/* Defaults for the job control signals, whatever ours are. */
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		execl("/bin/sh", "sh", "-i", "-m", (char *)NULL);
		_exit(2);
	}

	/* The control characters the shell's terminal uses. */
	if (tcgetattr(master, &t) != 0) e(5);

	type("PS1='" PROMPT "'\n");
	if (!expect(NULL, 1, 5000)) e(6);

	subtest = 2;
	/* ^Z stops the foreground job. */
	type("sleep 30\n");
	usleep(500000);
	key(t.c_cc[VSUSP]);
	if (!expect("Suspended", 1, 5000)) e(1);	/* ash's word for it */
	type("jobs\n");
	if (!expect("Suspended", 1, 5000)) e(2);

	subtest = 3;
	/* bg lets it run in the background. */
	type("bg\n");
	if (!expect(NULL, 1, 5000)) e(1);
	type("jobs\n");
	if (!expect("Running", 1, 5000)) e(2);

	subtest = 4;
	/* fg brings it back; ^C ends it. */
	type("fg\n");
	usleep(500000);
	key(t.c_cc[VINTR]);
	if (!expect(NULL, 1, 5000)) e(1);
	type("jobs; echo JOBS-DONE\n");
	if (!expect("JOBS-DONE", 1, 5000)) e(2);
	if (strstr(out, "sleep") != NULL &&
	    strstr(strstr(out, "jobs;") ? strstr(out, "jobs;") + 5 : out,
	    "sleep 30") != NULL) e(3);		/* no job left */

	subtest = 5;
	/* A background job reading the terminal is stopped. */
	type("cat &\n");
	if (!expect(NULL, 1, 5000)) e(1);
	{
		int i, stopped = 0;

		/* cat needs a moment to start and try its read.  This ash
		 * collects a background job's stop when it next waits for a
		 * foreground one (waitproc() asks for WUNTRACED only then), so
		 * let a short foreground command run before asking.
		 */
		for (i = 0; i < 10 && !stopped; i++) {
			type("sleep 1; jobs\n");
			if (!expect(NULL, 1, 5000)) e(2);
			stopped = (strstr(out, "Stopped") != NULL);
		}
		if (!stopped) {
			printf("no stop for cat in:\n%s\n", out);
			type("ps -alx | grep -v grep | grep cat\n");
			(void)expect(NULL, 1, 5000);
			printf("diagnostics:\n%s\n", out);
			e(3);
		}
	}
	type("kill %1\n");
	if (!expect(NULL, 1, 5000)) e(4);

	subtest = 6;
	type("exit\n");
	if (waitpid(sh, &status, 0) != sh) e(1);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("shell status 0x%x\n", status);
		e(2);
	}
	alarm(0);
	close(master);

	quit();
	return 0;
}
