#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/mman.h>
#include <string.h>

/*
 * msync(2).  Shared mappings of a file write to the file system's own cache
 * of it, so VM has the file flushed for MS_SYNC, and nothing to do else.
 */
int
__msync13(void *addr, size_t len, int flags)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_mprotect.addr = addr;
	m.m_lc_vm_mprotect.len = len;
	m.m_lc_vm_mprotect.prot = flags;	/* the msync flags */

	return _syscall(VM_PROC_NR, VM_MSYNC, &m);
}
