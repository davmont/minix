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
 * Each subtest hands an upper-half address to write() and read() on a pipe
 * and expects EFAULT.  Should the write() succeed, the read() gives back the
 * same bytes, so that a broken kernel is reported, not damaged.
 */
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

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

int
main(int argc, char **argv)
{

	start(138);

	test_addr(1, 0xffff800000000000UL);	/* physmap: physical 0 */
	test_addr(2, 0xffff800000100000UL);	/* physmap: physical 1 MB */
	test_addr(3, 0xffffffff80200000UL);	/* kernel image */
	test_addr(4, 0x0000800000000000UL);	/* non-canonical; walks
						 * like the physmap */
	test_addr(5, 0x00007ffffffffff8UL);	/* crosses the top of the
						 * user half */

	quit();
	return 0;
}
