/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
/* $Header$ */

#include	<lwp.h>
#include	<signal.h>

/* POSIX: raise() signals the calling thread, as pthread_kill(pthread_self())
 * would, and fails with EINVAL for a bad signal number (PM checks it). */
int
raise(int sig)
{
	return _lwp_kill(_lwp_self(), sig);
}
