/*	wlcoreprobe - exercise libwayland's event loop on MINIX
 *
 * libwayland's event loop is written against Linux's epoll, timerfd, signalfd
 * and eventfd.  MINIX has none of them, so external/mit/wayland/minix emulates
 * that slice of the API on top of poll(2) and src/event-loop.c is compiled
 * unmodified against it.
 *
 * A library that links proves nothing about an emulation.  This probe drives
 * the real wl_event_loop through each of the four emulated primitives:
 *
 *	file descriptor sources	-> the epoll emulation
 *	timer sources		-> the timerfd emulation
 *	signal sources		-> the signalfd emulation
 *	wl_display_terminate	-> the eventfd emulation
 *
 * plus idle sources and a real listening socket, which is what a compositor
 * actually starts up with.
 *
 * Only libwayland-server is linked: MINIX links statically by default, and the
 * server and client libraries each carry the generated wl_interface tables, so
 * one binary cannot have both.  The client side (and with it protocol dispatch
 * through ffi_call) is covered separately -- see ffiprobe for the ABI itself.
 *
 * Exits 0 only if every check passes.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	if (ok) {
		printf("%-46s -> OK\n", what);
	} else {
		printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	fflush(stdout);
}

/* ---- fd source: the epoll emulation ---------------------------------- */

static int fd_fired;

static int
on_readable(int fd, uint32_t mask, void *data)
{
	char c;

	(void)data;
	if (mask & WL_EVENT_READABLE) {
		if (read(fd, &c, 1) == 1)
			fd_fired++;
	}
	return 0;
}

/* ---- timer source: the timerfd emulation ----------------------------- */

static int timer_fired;

static int
on_timer(void *data)
{
	(void)data;
	timer_fired++;
	return 0;
}

/* ---- idle source ------------------------------------------------------ */

static int idle_fired;

static void
on_idle(void *data)
{
	(void)data;
	idle_fired++;
}

/* ---- signal source: the signalfd emulation ---------------------------- */

static int signal_fired;

static int
on_signal(int signum, void *data)
{
	(void)data;
	if (signum == SIGUSR1)
		signal_fired++;
	return 0;
}

/* ---- terminate: the eventfd emulation --------------------------------- */

static struct wl_display *term_display;

static int
on_terminate_timer(void *data)
{
	(void)data;
	/* wl_display_terminate() writes to libwayland's eventfd to break
	 * wl_display_run() out of its dispatch loop. */
	wl_display_terminate(term_display);
	return 0;
}

static long
elapsed_ms(struct timespec *from)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - from->tv_sec) * 1000 +
	    (now.tv_nsec - from->tv_nsec) / 1000000;
}

int
main(void)
{
	struct wl_event_loop *loop;
	struct wl_event_source *fd_src, *timer_src, *sig_src;
	struct wl_display *display;
	struct timespec start;
	const char *sock;
	int pfd[2];
	int i;

	printf("wlcoreprobe: libwayland event loop on the poll(2) emulation\n\n");

	/* wl_display_add_socket_auto() puts the listening socket in
	 * XDG_RUNTIME_DIR; without it libwayland refuses to bind. */
	if (getenv("XDG_RUNTIME_DIR") == NULL)
		setenv("XDG_RUNTIME_DIR", "/tmp", 1);

	loop = wl_event_loop_create();
	check("wl_event_loop_create", loop != NULL, "returned NULL");
	if (loop == NULL)
		return 1;

	/* 1. fd source -- the epoll emulation. */
	if (pipe(pfd) != 0) {
		check("pipe for fd source", 0, strerror(errno));
		return 1;
	}
	fd_src = wl_event_loop_add_fd(loop, pfd[0], WL_EVENT_READABLE,
	    on_readable, NULL);
	check("wl_event_loop_add_fd", fd_src != NULL, "returned NULL");

	if (write(pfd[1], "x", 1) != 1)
		check("write to pipe", 0, strerror(errno));

	wl_event_loop_dispatch(loop, 500);
	check("epoll emulation: fd source dispatched", fd_fired == 1,
	    "callback did not fire exactly once");

	/* 2. idle source -- runs once, after dispatch. */
	wl_event_loop_add_idle(loop, on_idle, NULL);
	wl_event_loop_dispatch(loop, 0);
	check("idle source dispatched", idle_fired == 1,
	    "callback did not fire exactly once");

	/*
	 * 3. timer source -- the timerfd emulation.  Arm for 60 ms and dispatch
	 * with a generous timeout; the loop must block, not spin, and must wake
	 * when the deadline arrives rather than immediately.
	 */
	timer_src = wl_event_loop_add_timer(loop, on_timer, NULL);
	check("wl_event_loop_add_timer", timer_src != NULL, "returned NULL");

	clock_gettime(CLOCK_MONOTONIC, &start);
	wl_event_source_timer_update(timer_src, 60);

	for (i = 0; i < 20 && timer_fired == 0; i++)
		wl_event_loop_dispatch(loop, 200);

	check("timerfd emulation: timer fired", timer_fired == 1,
	    "timer callback did not fire");
	{
		long ms = elapsed_ms(&start);
		char detail[64];

		/* Firing early would mean the deadline was ignored and the loop
		 * merely spun; that is the failure mode worth catching. */
		snprintf(detail, sizeof(detail), "fired after %ld ms", ms);
		check("timerfd emulation: waited for the deadline",
		    ms >= 55, detail);
	}

	/* A disarmed timer must not fire again. */
	timer_fired = 0;
	wl_event_source_timer_update(timer_src, 0);	/* 0 disarms */
	wl_event_loop_dispatch(loop, 100);
	check("timerfd emulation: disarmed timer stays quiet",
	    timer_fired == 0, "timer fired after being disarmed");

	/*
	 * 4. signal source -- the signalfd emulation.  wl_event_loop_add_signal()
	 * blocks the signal (correct for a real signalfd); the emulation has to
	 * undo that and deliver via a handler, or this never fires.
	 */
	sig_src = wl_event_loop_add_signal(loop, SIGUSR1, on_signal, NULL);
	check("wl_event_loop_add_signal", sig_src != NULL, "returned NULL");

	raise(SIGUSR1);

	for (i = 0; i < 20 && signal_fired == 0; i++)
		wl_event_loop_dispatch(loop, 100);

	check("signalfd emulation: signal source dispatched",
	    signal_fired == 1, "callback did not fire");

	wl_event_source_remove(fd_src);
	wl_event_source_remove(timer_src);
	wl_event_source_remove(sig_src);
	wl_event_loop_destroy(loop);
	close(pfd[0]);
	close(pfd[1]);

	/*
	 * 5. A real display: create it, advertise wl_shm, and bind a listening
	 * socket -- exactly what a compositor does before its first client.
	 */
	display = wl_display_create();
	check("wl_display_create", display != NULL, "returned NULL");
	if (display == NULL)
		return 1;

	check("wl_display_init_shm (advertises wl_shm)",
	    wl_display_init_shm(display) == 0, "init_shm failed");

	sock = wl_display_add_socket_auto(display);
	check("wl_display_add_socket_auto", sock != NULL, strerror(errno));
	if (sock != NULL) {
		char path[256];
		struct stat st;

		snprintf(path, sizeof(path), "%s/%s",
		    getenv("XDG_RUNTIME_DIR"), sock);
		printf("%-46s -> %s\n", "  listening socket", path);
		check("listening socket exists on disk",
		    stat(path, &st) == 0, strerror(errno));
	}

	/*
	 * 6. wl_display_run() blocks in the event loop; wl_display_terminate()
	 * breaks it out by writing to libwayland's eventfd.  If the eventfd
	 * emulation were wrong this would hang rather than fail, so the timer
	 * that calls terminate is the only thing that can end this test.
	 */
	term_display = display;
	timer_src = wl_event_loop_add_timer(wl_display_get_event_loop(display),
	    on_terminate_timer, NULL);
	wl_event_source_timer_update(timer_src, 100);

	printf("%-46s -> (running)\n", "  wl_display_run, terminate in 100ms");
	fflush(stdout);

	wl_display_run(display);	/* must return, not hang */

	check("eventfd emulation: wl_display_run returned after terminate",
	    1, "");

	wl_display_destroy(display);

	printf("\nwlcoreprobe: %s\n",
	    failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
