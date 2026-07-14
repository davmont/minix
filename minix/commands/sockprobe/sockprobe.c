/* Why can dbus not create a unix socket when libwayland can?
 *
 * Both call socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0), and dbus gets ENOENT.
 * Ask the system directly, one variable at a time: with the flag, without it,
 * and then whether the resulting socket can actually be bound and listened on.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
try_socket(const char *what, int type)
{
	int fd = socket(AF_UNIX, type, 0);

	if (fd < 0)
		printf("  %-28s FAILED errno=%d (%s)\n", what, errno,
		    strerror(errno));
	else {
		printf("  %-28s ok (fd %d)\n", what, fd);
		close(fd);
	}
}

int
main(void)
{
	struct sockaddr_un sun;
	int fd;

	printf("socket(AF_UNIX, ...):\n");
	try_socket("SOCK_STREAM", SOCK_STREAM);
	try_socket("SOCK_STREAM|SOCK_CLOEXEC", SOCK_STREAM | SOCK_CLOEXEC);
	try_socket("SOCK_STREAM|SOCK_NONBLOCK", SOCK_STREAM | SOCK_NONBLOCK);
	try_socket("SOCK_DGRAM", SOCK_DGRAM);

	/* A socket you cannot bind is no use to a bus daemon. */
	printf("bind+listen /tmp/sockprobe.sock:\n");
	(void)unlink("/tmp/sockprobe.sock");
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		printf("  socket FAILED errno=%d (%s)\n", errno,
		    strerror(errno));
		return 1;
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, "/tmp/sockprobe.sock", sizeof(sun.sun_path));
	if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0)
		printf("  bind FAILED errno=%d (%s)\n", errno, strerror(errno));
	else if (listen(fd, 5) < 0)
		printf("  listen FAILED errno=%d (%s)\n", errno,
		    strerror(errno));
	else
		printf("  bind+listen ok\n");
	close(fd);
	return 0;
}
