/* Test 134 - getcwd() while other threads use relative names.
 *
 * The working directory belongs to the process, so getcwd() must not change
 * it even for a moment: a thread using a relative name meanwhile would find
 * another file, or none.  getcwd() used to chdir("..") up to the root and
 * back down.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define LOOPS		2000
#define USERS		2

static char expect[PATH_MAX];
static int stop;
static int cwd_errors, name_errors;

static void *
cwd_loop(void *arg)
{
	char buf[PATH_MAX];
	int i;

	for (i = 0; i < LOOPS; i++) {
		if (getcwd(buf, sizeof(buf)) == NULL ||
		    strcmp(buf, expect) != 0)
			__atomic_fetch_add(&cwd_errors, 1, __ATOMIC_SEQ_CST);
	}
	__atomic_store_n(&stop, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void *
name_loop(void *arg)
{
	struct stat st;
	char name[16];
	int fd;

	snprintf(name, sizeof(name), "new%d", (int)(long)arg);
	while (!__atomic_load_n(&stop, __ATOMIC_SEQ_CST)) {
		if (stat("here", &st) != 0 ||
		    (fd = open("here", O_RDONLY)) < 0) {
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
			continue;
		}
		close(fd);
		if ((fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
			continue;
		}
		close(fd);
		if (unlink(name) != 0)
			__atomic_fetch_add(&name_errors, 1, __ATOMIC_SEQ_CST);
	}
	return NULL;
}

static void
test_concurrent(void)
{
	pthread_t c, u[USERS];
	int fd, i;

	subtest = 1;
	if (mkdir("a", 0755) != 0 || mkdir("a/b", 0755) != 0) e(1);
	if (chdir("a/b") != 0) e(2);
	if ((fd = open("here", O_RDWR | O_CREAT, 0644)) < 0) e(3);
	close(fd);
	if (getcwd(expect, sizeof(expect)) == NULL) e(4);
	if (strlen(expect) < 4 || strcmp(expect + strlen(expect) - 4,
	    "/a/b") != 0) {
		printf("getcwd: %s\n", expect);
		e(5);
	}

	for (i = 0; i < USERS; i++)
		if (pthread_create(&u[i], NULL, name_loop, (void *)(long)i))
			e(6);
	if (pthread_create(&c, NULL, cwd_loop, NULL) != 0) e(7);
	if (pthread_join(c, NULL) != 0) e(8);
	for (i = 0; i < USERS; i++)
		if (pthread_join(u[i], NULL) != 0) e(9);
	if (cwd_errors != 0) {
		printf("%d bad getcwd() results\n", cwd_errors);
		e(10);
	}
	if (name_errors != 0) {
		printf("%d failed relative names\n", name_errors);
		e(11);
	}
	if (chdir("../..") != 0) e(12);
}

static void
test_names(void)
{
	char buf[PATH_MAX], home[PATH_MAX], small[3], *m;

	subtest = 2;
	if (getcwd(home, sizeof(home)) == NULL) e(10);
	if (chdir("/") != 0) e(1);
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/") != 0) e(2);
	if (chdir("/usr/bin") != 0) e(3);
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/usr/bin") != 0)
		e(4);
	if (getcwd(small, sizeof(small)) != NULL || errno != ERANGE) e(5);
	/* A failed getcwd() does not move the working directory. */
	if (getcwd(buf, sizeof(buf)) == NULL || strcmp(buf, "/usr/bin") != 0)
		e(6);
	if ((m = getcwd(NULL, 0)) == NULL || strcmp(m, "/usr/bin") != 0) e(7);
	free(m);
	if (chdir(home) != 0) e(8);
}

int
main(int argc, char **argv)
{

	start(134);

	test_concurrent();
	test_names();

	quit();
	return 0;
}
