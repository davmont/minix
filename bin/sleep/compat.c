#include <time.h>
#include <errno.h>

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

int clock_nanosleep(clockid_t clock_id, int flags,
    const struct timespec *request, struct timespec *remain)
{
    struct timespec rel;
    if (flags & TIMER_ABSTIME) {
        struct timespec now;
        if (clock_gettime(clock_id, &now) == -1)
            return errno;
        if (request->tv_sec < now.tv_sec ||
            (request->tv_sec == now.tv_sec && request->tv_nsec <= now.tv_nsec))
            return 0;
        rel.tv_sec = request->tv_sec - now.tv_sec;
        rel.tv_nsec = request->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0) {
            rel.tv_sec--;
            rel.tv_nsec += 1000000000;
        }
    } else {
        rel = *request;
    }

    if (nanosleep(&rel, remain) == -1)
        return errno;
    return 0;
}
