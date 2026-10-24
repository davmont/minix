/* Test 156 - alarm() and setitimer(ITIMER_REAL) in a threaded process.
 *
 * The real-time timer is the process's (POSIX).  PM kept it in the slot of
 * the thread that set it, so another thread did not see it, and when that
 * thread exited the timer stayed queued: once a new process took the slot,
 * PM's timer list was corrupt and PM crashed on a call through NULL.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static volatile sig_atomic_t alarms;

static void
on_alarm(int sig)
{
	alarms++;
}

/* Wait up to three seconds for an alarm of this process. */
static int
got_alarm(void)
{
	int i;

	for (i = 0; i < 30 && alarms == 0; i++)
		usleep(100000);
	return alarms != 0;
}

static void *
set_alarm(void *arg)
{
	alarm((unsigned int)(long)arg);
	return NULL;
}

static void *
set_itimer(void *arg)
{
	struct itimerval it;

	it.it_value.tv_sec = 30;
	it.it_value.tv_usec = 0;
	it.it_interval.tv_sec = 30;
	it.it_interval.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0)
		return arg;
	return NULL;
}

/* A thread's alarm is the process's: the main thread sees it, cancels it
 * and is signalled by it after the thread has gone.
 */
static void
test156a(void)
{
	pthread_t t;
	struct itimerval it;
	void *res;
	unsigned int left;

	subtest = 1;

	if (signal(SIGALRM, on_alarm) == SIG_ERR) e(1);

	if (pthread_create(&t, NULL, set_alarm, (void *)10L) != 0) e(2);
	if (pthread_join(t, NULL) != 0) e(3);
	left = alarm(0);
	if (left < 8 || left > 10) e(4);

	if (pthread_create(&t, NULL, set_itimer, NULL) != 0) e(5);
	if (pthread_join(t, &res) != 0 || res != NULL) e(6);
	if (getitimer(ITIMER_REAL, &it) != 0) e(7);
	if (it.it_value.tv_sec < 28 || it.it_interval.tv_sec != 30) e(8);
	memset(&it, 0, sizeof(it));
	if (setitimer(ITIMER_REAL, &it, NULL) != 0) e(9);

	alarms = 0;
	if (pthread_create(&t, NULL, set_alarm, (void *)1L) != 0) e(10);
	if (pthread_join(t, NULL) != 0) e(11);
	if (!got_alarm()) e(12);
}

/* A thread exits with its alarm pending, and its process with it; then
 * forks go round the whole process table, through the thread's old slot,
 * and the alarm's time passes.  PM must stay up, and its timers work.
 */
static void
test156b(void)
{
	pthread_t t;
	pid_t pid;
	int i, status;

	subtest = 2;

	/* The forks copy this process's slot, timer included: give the timer a
	 * recent expiry time, as a program that used alarm() has.  (With a
	 * stale one, the copy looks not due, and PM's timers stop instead.)
	 */
	if (signal(SIGALRM, on_alarm) == SIG_ERR) e(1);
	alarms = 0;
	alarm(1);
	if (!got_alarm()) e(1);

	if ((pid = fork()) == 0) {
		signal(SIGALRM, SIG_IGN);
		if (pthread_create(&t, NULL, set_alarm, (void *)3L) != 0 ||
		    pthread_join(t, NULL) != 0)
			_exit(1);
		_exit(0);
	}
	if (pid < 0) e(1);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(2);

	for (i = 0; i < 600; i++) {
		if ((pid = fork()) == 0)
			_exit(0);
		if (pid < 0) e(3);
		if (waitpid(pid, &status, 0) != pid || status != 0) e(4);
	}
	sleep(4);			/* the alarm's time has passed */
	if ((pid = fork()) == 0)
		_exit(0);
	if (pid < 0) e(5);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(6);

	alarms = 0;
	alarm(1);
	if (!got_alarm()) e(7);
}

int
main(int argc, char **argv)
{
	start(156);

	test156a();
	test156b();

	quit();
	return 0;
}
