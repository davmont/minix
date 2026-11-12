#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

/*
 * mlock(2), munlock(2), mlockall(2) and munlockall(2).  VM pins the pages:
 * it brings them in, and neither compresses, swaps out nor evicts them until
 * they are unlocked, unmapped, or the process exits or execs.  A child of
 * fork(2) does not inherit the locks.  The superuser may lock any amount;
 * others up to their RLIMIT_MEMLOCK.
 */

static int
vm_mlock(int op, const void *addr, size_t len, int flags)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_mlock.op = op;
	m.m_lc_vm_mlock.addr = __UNCONST(addr);
	m.m_lc_vm_mlock.len = len;
	m.m_lc_vm_mlock.flags = flags;
	return _syscall(VM_PROC_NR, VM_MLOCK, &m) < 0 ? -1 : 0;
}

int
mlock(const void *addr, size_t len)
{

	return vm_mlock(VMML_LOCK, addr, len, 0);
}

int
munlock(const void *addr, size_t len)
{

	return vm_mlock(VMML_UNLOCK, addr, len, 0);
}

int
mlockall(int flags)
{

	return vm_mlock(VMML_LOCKALL, NULL, 0, flags);
}

int
munlockall(void)
{

	return vm_mlock(VMML_UNLOCKALL, NULL, 0, 0);
}
