/* minix_rand32: 32 random bits for system services, which cannot read
 * /dev/random (it is a driver they may run before, or be).  Not for keys:
 * for address-space randomization and the like.
 *
 * RDRAND where the CPU has it (CPUID.1:ECX bit 30); otherwise, and should
 * RDRAND keep failing, splitmix64 over the time-stamp counter, mixed into
 * a per-process state so that consecutive calls differ.
 */
#include <minix/sysutil.h>

#if defined(__i386__) || defined(__x86_64__)
static int
have_rdrand(void)
{
	static int known = 0, have = 0;
	u32_t a, b, c, d;

	if (!known) {
		__asm__ __volatile__("cpuid"
		    : "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (1), "c" (0));
		have = (c >> 30) & 1;
		known = 1;
	}
	return have;
}

static int
rdrand32(u32_t *v)
{
	unsigned char ok;
	int i;

	for (i = 0; i < 10; i++) {
		__asm__ __volatile__("rdrand %0; setc %1" : "=r" (*v), "=qm" (ok));
		if (ok)
			return 1;
	}
	return 0;
}

static u64_t
counter(void)
{
	u32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((u64_t)hi << 32) | lo;
}
#else
static u64_t
counter(void)
{

	return (u64_t)getticks();
}
#endif

u32_t
minix_rand32(void)
{
	static u64_t state;
	u64_t z;

#if defined(__i386__) || defined(__x86_64__)
	u32_t v;

	if (have_rdrand() && rdrand32(&v))
		return v;
#endif
	/* splitmix64 */
	state += 0x9e3779b97f4a7c15ULL ^ counter();
	z = state;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	z ^= z >> 31;
	return (u32_t)(z >> 32);
}
