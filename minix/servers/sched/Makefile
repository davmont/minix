# Makefile for Scheduler (SCHED)
CONFIG_SMP=	yes
CONFIG_MAX_CPUS= 32

PROG=	sched
SRCS=	main.c schedule.c utility.c

DPADD+=	${LIBSYS}
LDADD+=	-lsys

.include <minix.service.mk>
