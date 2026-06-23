/*
rdrand.c

Hardware random-number-generator (RDRAND) support for the random driver.

On x86 the CPU's on-chip RNG (Intel "Bull Mountain" RDRAND, available since
Ivy Bridge and on all AMD parts since ~2015) provides high-quality entropy
with no I/O.  We use it to seed the pool at startup so that /dev/random and
/dev/urandom are usable immediately after boot, instead of racing the slow
IRQ-timing entropy source (which, in a quiet/headless virtual machine, can
take an unpredictable amount of time to gather MIN_SAMPLES).

On architectures without RDRAND (or older x86 parts) arch_hw_random() simply
returns 0 and the driver falls back to its IRQ/TSC entropy gathering.
*/

#include <minix/drivers.h>
#include <minix/minlib.h>
#include <string.h>

#include "random.h"

#if defined(__i386__) || defined(__x86_64__)

#define CPUID1_ECX_RDRAND	(1U << 30)	/* CPUID leaf 1, ECX bit 30 */

static int
have_rdrand(void)
{
	u32_t eax, ebx, ecx, edx;

	eax = 1;
	ebx = ecx = edx = 0;
	_cpuid(&eax, &ebx, &ecx, &edx);

	return (ecx & CPUID1_ECX_RDRAND) != 0;
}

/*
 * Read one machine word from RDRAND.  The carry flag is set by the CPU when a
 * random value was successfully returned; a handful of retries covers the rare
 * transient "not ready" case.  Returns 1 on success, 0 on repeated failure.
 */
static int
rdrand_word(unsigned long *out)
{
	unsigned char ok;
	int tries;

	for (tries = 0; tries < 32; tries++) {
		__asm__ __volatile__ (
			"rdrand %0; setc %1"
			: "=r" (*out), "=qm" (ok)
			:
			: "cc");
		if (ok)
			return 1;
	}
	return 0;
}

/*
 * Fill buf with up to nbytes of hardware randomness.  Returns the number of
 * bytes actually obtained (0 if RDRAND is unavailable).
 */
int
arch_hw_random(void *buf, size_t nbytes)
{
	unsigned char *p = buf;
	size_t done = 0;
	unsigned long w;

	if (!have_rdrand())
		return 0;

	while (done < nbytes) {
		size_t chunk = nbytes - done;

		if (chunk > sizeof(w))
			chunk = sizeof(w);
		if (!rdrand_word(&w))
			break;
		memcpy(p + done, &w, chunk);
		done += chunk;
	}

	return (int)done;
}

#else /* !x86: no hardware RNG instruction */

int
arch_hw_random(void *buf, size_t nbytes)
{
	return 0;
}

#endif
