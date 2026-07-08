/*
 * This stub exists becuase we know a modern BSD supports all TAILQ
 * and glibc, musl et all, don't.
 */
/* MINIX: clang's *-elf64-minix target predefines neither __unix__ nor unix,
 * but MINIX is a BSD-queue system (<sys/param.h> defines BSD). */
#if (defined(__unix__) || defined(unix) || defined(__minix)) && !defined(USG)
#include <sys/param.h>
#endif
#ifdef BSD
#include <sys/queue.h>
/* Dragonfly BSD needs this :( */
#if !defined(TAILQ_FOREACH_SAFE) && defined(TAILQ_FOREACH_MUTABLE)
#define	TAILQ_FOREACH_SAFE	TAILQ_FOREACH_MUTABLE
#endif
#else
#include "../vendor/queue.h"
#endif
