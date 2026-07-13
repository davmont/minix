/*	MINIX: private signalfd emulation for libwayland.  See sys/epoll.h. */

#ifndef MINIX_WL_SYS_SIGNALFD_H
#define MINIX_WL_SYS_SIGNALFD_H

#include <sys/epoll.h>		/* for the close(2) redirect */
#include <signal.h>
#include <stdint.h>

#define SFD_CLOEXEC	0x400000
#define SFD_NONBLOCK	0x004000

/*
 * Linux's signalfd delivers signals as records read from a descriptor.  Here a
 * signalfd is a self-pipe: signalfd() installs a handler for each signal in the
 * set, and the handler write(2)s one of these records into the pipe (write is
 * async-signal-safe).  libwayland read(2)s the descriptor directly and expects
 * exactly sizeof(struct signalfd_siginfo) bytes, so the record must be written
 * whole; the layout below matches Linux's 128-byte struct for that reason.
 *
 * NOTE a real semantic difference: wl_event_loop_add_signal() calls
 * sigprocmask(SIG_BLOCK) before signalfd(), because with a true signalfd the
 * signal must NOT be delivered normally.  A blocked signal would never run our
 * handler, so this shim unblocks it again and relies on handler delivery.  The
 * observable behaviour for libwayland is the same -- the fd becomes readable
 * and carries the signal number -- but the process does take an actual signal.
 */
struct signalfd_siginfo {
	uint32_t ssi_signo;
	int32_t  ssi_errno;
	int32_t  ssi_code;
	uint32_t ssi_pid;
	uint32_t ssi_uid;
	int32_t  ssi_fd;
	uint32_t ssi_tid;
	uint32_t ssi_band;
	uint32_t ssi_overrun;
	uint32_t ssi_trapno;
	int32_t  ssi_status;
	int32_t  ssi_int;
	uint64_t ssi_ptr;
	uint64_t ssi_utime;
	uint64_t ssi_stime;
	uint64_t ssi_addr;
	uint16_t ssi_addr_lsb;
	uint8_t  __pad[46];
};

int signalfd(int fd, const sigset_t *mask, int flags);

#endif /* MINIX_WL_SYS_SIGNALFD_H */
