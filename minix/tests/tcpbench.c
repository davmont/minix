/* tcpbench - measure TCP throughput between two hosts.
 *
 *   tcpbench -l port			receive, report bytes and MB/s
 *   tcpbench host port megabytes	send, report MB/s
 *
 * A helper for checking TCP performance by hand (see
 * releasetools/devtools/netbench.py); not run by the test suite.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
	static char buf[65536];
	struct sockaddr_in sin;
	long long total = 0, want;
	double t0, t;
	ssize_t r;
	int s, c, on = 1;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;

	if (argc == 3 && strcmp(argv[1], "-l") == 0) {
		if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) err(1, "socket");
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
		sin.sin_port = htons(atoi(argv[2]));
		if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
			err(1, "bind");
		if (listen(s, 1) < 0) err(1, "listen");
		if ((c = accept(s, NULL, NULL)) < 0) err(1, "accept");
		t0 = now();
		while ((r = read(c, buf, sizeof(buf))) > 0)
			total += r;
		t = now() - t0;
		printf("received %lld bytes in %.2f s: %.2f MB/s\n", total, t,
		    total / t / 1e6);
		return 0;
	}

	if (argc != 4) {
		fprintf(stderr, "usage: tcpbench -l port | host port MB\n");
		return 2;
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) err(1, "socket");
	if (inet_aton(argv[1], &sin.sin_addr) == 0) errx(1, "bad address");
	sin.sin_port = htons(atoi(argv[2]));
	want = atoll(argv[3]) * 1000000LL;
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "connect");
	t0 = now();
	while (total < want) {
		size_t n = want - total < (long long)sizeof(buf) ?
		    (size_t)(want - total) : sizeof(buf);
		if ((r = write(s, buf, n)) <= 0) err(1, "write");
		total += r;
	}
	shutdown(s, SHUT_WR);
	while (read(s, buf, sizeof(buf)) > 0)
		;
	t = now() - t0;
	printf("sent %lld bytes in %.2f s: %.2f MB/s\n", total, t,
	    total / t / 1e6);
	return 0;
}
