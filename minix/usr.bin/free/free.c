/* free -- display system memory usage (MINIX) */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

int
main(int argc, char **argv)
{
	struct uvmexp_sysctl u;
	size_t len = sizeof(u);
	int ch, human = 0, shift = 10;	/* default: KiB */
	const char *unit = "kB";

	while ((ch = getopt(argc, argv, "bkmgh")) != -1) {
		switch (ch) {
		case 'b': shift = 0;  unit = "B";  break;
		case 'k': shift = 10; unit = "kB"; break;
		case 'm': shift = 20; unit = "MB"; break;
		case 'g': shift = 30; unit = "GB"; break;
		case 'h': human = 1; break;
		default:
			fprintf(stderr, "usage: free [-b|-k|-m|-g|-h]\n");
			return 1;
		}
	}

	memset(&u, 0, sizeof(u));
	if (sysctlbyname("vm.uvmexp2", &u, &len, NULL, 0) == -1)
		err(1, "sysctl vm.uvmexp2");

	long long ps     = u.pagesize;
	long long total  = (long long)u.npages    * ps;
	long long freeb  = (long long)u.free      * ps;
	long long cached = (long long)u.filepages * ps;
	long long used   = total - freeb;

	if (human) {
		static const char *suf[] = { "B","K","M","G","T" };
		char b[5][16];
		long long v[5] = { total, used, freeb, cached };
		for (int i = 0; i < 4; i++) {
			double d = (double)v[i]; int s = 0;
			while (d >= 1024.0 && s < 4) { d /= 1024.0; s++; }
			snprintf(b[i], sizeof(b[i]), "%.1f%s", d, suf[s]);
		}
		printf("%14s %10s %10s %10s %10s\n", "", "total", "used", "free", "cached");
		printf("%-14s %10s %10s %10s %10s\n", "Mem:", b[0], b[1], b[2], b[3]);
	} else {
		printf("%14s %12s %12s %12s %12s\n", "", "total", "used", "free", "cached");
		printf("%-14s %12lld %12lld %12lld %12lld   (%s)\n", "Mem:",
		    total >> shift, used >> shift, freeb >> shift, cached >> shift, unit);
	}
	return 0;
}
