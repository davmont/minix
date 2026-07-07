/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * MINIX 3 platform support for libuv.  MINIX has no kqueue/epoll, so the
 * portable poll(2) event loop (posix-poll.c) is used; this file provides only
 * the small set of platform-specific system-info helpers, modelled on the
 * other poll(2)-based ports (cygwin.c / haiku.c).  Several queries MINIX does
 * not expose are returned as conservative defaults (0 / UV_ENOSYS), matching
 * what those ports do for the same gaps.
 */

#include "uv.h"
#include "internal.h"

#include <time.h>
#include <unistd.h>

void uv_loadavg(double avg[3]) {
  /* MINIX does not maintain a load average. */
  avg[0] = 0;
  avg[1] = 0;
  avg[2] = 0;
}


int uv_exepath(char* buffer, size_t* size) {
  /* MINIX has no /proc/self/exe equivalent. */
  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  return UV_ENOSYS;
}


uint64_t uv_get_free_memory(void) {
#if defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long psize = sysconf(_SC_PAGESIZE);

  if (pages > 0 && psize > 0)
    return (uint64_t) pages * (uint64_t) psize;
#endif
  return 0;
}


uint64_t uv_get_total_memory(void) {
#if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
  long pages = sysconf(_SC_PHYS_PAGES);
  long psize = sysconf(_SC_PAGESIZE);

  if (pages > 0 && psize > 0)
    return (uint64_t) pages * (uint64_t) psize;
#endif
  return 0;
}


uint64_t uv_get_constrained_memory(void) {
  return uv__get_rlimit_max_memory();
}


uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}


int uv_resident_set_memory(size_t* rss) {
  /* MINIX exposes no per-process RSS to userland; report 0. */
  *rss = 0;
  return 0;
}


int uv_uptime(double* uptime) {
  struct timespec now;

  /* CLOCK_MONOTONIC counts from boot on MINIX. */
  if (clock_gettime(CLOCK_MONOTONIC, &now))
    return UV__ERR(errno);

  *uptime = (double) now.tv_sec + (double) now.tv_nsec / 1e9;
  return 0;
}


int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  uv_cpu_info_t* ci;
  int n;
  int i;

  if (cpu_infos == NULL || count == NULL)
    return UV_EINVAL;

  n = (int) sysconf(_SC_NPROCESSORS_ONLN);
  if (n <= 0)
    n = 1;

  ci = uv__calloc(n, sizeof(*ci));
  if (ci == NULL)
    return UV_ENOMEM;

  /* MINIX does not report per-CPU model/speed/times; fill placeholders so
   * callers (e.g. thread-pool sizing) still get an accurate CPU count. */
  for (i = 0; i < n; i++) {
    ci[i].model = uv__strdup("unknown");
    ci[i].speed = 0;
    /* ci[i].cpu_times is already zeroed by uv__calloc. */
  }

  *cpu_infos = ci;
  *count = n;
  return 0;
}
