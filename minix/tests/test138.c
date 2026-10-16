/* Test 138 - a process cannot name kernel memory as its own.
 *
 * Every amd64 address space maps the kernel image and the physmap (all of
 * physical memory) in the upper half, without the user bit.  When the kernel
 * copies to or from a process buffer, it translates the address with the
 * process's page tables and goes through the physmap, where page protection
 * does not apply; so the translation itself must refuse pages the process
 * cannot reach.  When it did not, write() from a kernel address leaked kernel
 * and physical memory to any process, and read() into one wrote it.
 *
 * Subtests 1-5 hand an upper-half address to write() and read() on a pipe
 * and expect EFAULT.  Should the write() succeed, the read() gives back the
 * same bytes, so that a broken kernel is reported, not damaged.
 *
 * IPC messages are another way in: the kernel copies them at the pointer the
 * process passes, directly.  Subtests 6-8 make SENDREC calls to PM, in a
 * child, with the message in kernel text or across the top of the user half,
 * and expect EFAULT.  Kernel text is read-only (CR0.WP, on every CPU), so a
 * kernel that takes the message there cannot store the reply into it.
 *
 * The addresses are amd64's.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <minix/ipc.h>
#include <minix/com.h>
#include <minix/ipcconst.h>

#include "common.h"

int max_error = 4;

static void
test_addr(int sub, unsigned long addr)
{
	unsigned char buf[16];
	int p[2];
	ssize_t r;

	subtest = sub;
	if (pipe(p) < 0) { e(1); return; }

	/* Out of the kernel. */
	r = write(p[1], (void *)addr, sizeof(buf));
	if (r != -1 || errno != EFAULT) {
		e(2);
		if (r > 0) {
			/* Leaked; put the same bytes back below. */
			if (read(p[0], buf, (size_t)r) != r) e(3);
			if (write(p[1], buf, (size_t)r) != r) e(4);
		}
	} else if (write(p[1], "0123456789abcdef", sizeof(buf)) != sizeof(buf))
		e(5);

	/* Into the kernel: the pipe holds 16 bytes for it. */
	r = read(p[0], (void *)addr, sizeof(buf));
	if (r != -1 || errno != EFAULT) e(6);

	close(p[0]);
	close(p[1]);
}

#if defined(__x86_64__)
/* SENDREC with SYSCALL, so that the pointer reaches the kernel as given. */
static long
raw_sendrec(endpoint_t dst, unsigned long m)
{
	long ax = dst, bx = SENDREC, di = IPCVEC_UM;
	register long r10 __asm__("r10") = (long)m;

	__asm__ __volatile__("syscall"
	    : "+a" (ax), "+b" (bx), "+D" (di), "+r" (r10)
	    : : "rcx", "rdx", "rsi", "r8", "r9", "r11", "memory", "cc");
	return ax;
}

static void
test_ipc(int sub, unsigned long addr)
{
	pid_t pid;
	int status;

	subtest = sub;
	if ((pid = fork()) == 0)
		_exit((int)raw_sendrec(PM_PROC_NR, addr) == -EFAULT ? 0 : 1);
	if (pid < 0) { e(1); return; }
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(3);
}
#endif

int
main(int argc, char **argv)
{

	start(138);

#if defined(__x86_64__)

	test_addr(1, 0xffff800000000000UL);	/* physmap: physical 0 */
	test_addr(2, 0xffff800000100000UL);	/* physmap: physical 1 MB */
	test_addr(3, 0xffffffff80200000UL);	/* kernel image */
	test_addr(4, 0x0000800000000000UL);	/* non-canonical; walks
						 * like the physmap */
	test_addr(5, 0x00007ffffffffff8UL);	/* crosses the top of the
						 * user half */

	test_ipc(6, 0xffffffff80250000UL);	/* kernel text */
	test_ipc(7, 0x0000800000000000UL);	/* non-canonical */
	test_ipc(8, 0x00007fffffffffc0UL);	/* crosses the top of the
						 * user half */

#endif
	quit();
	return 0;
}
