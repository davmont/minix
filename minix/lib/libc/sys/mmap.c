#define _SYSTEM 1
#define _MINIX_SYSTEM 1
#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>
#include <minix/u64.h>
#include <minix/vm.h>

/* INCLUDES HERE */

#include <machine/vmparam.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <minix/rs.h>
#include <minix/dmap.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#ifdef __weak_alias
__weak_alias(mmap, _mmap)
__weak_alias(munmap, _munmap)
#endif

void *minix_mmap_for(endpoint_t forwhom,
	void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_mmap.addr = addr;
	m.m_mmap.len = len;
	m.m_mmap.prot = prot;
	m.m_mmap.flags = flags;
	m.m_mmap.fd = fd;
	m.m_mmap.offset = offset;
	m.m_mmap.forwhom = forwhom;

	if(forwhom != SELF) {
		m.m_mmap.flags |= MAP_THIRDPARTY;
	}

	r = _syscall(VM_PROC_NR, VM_MMAP, &m);

	if(r != OK) {
		return MAP_FAILED;
	}

	return m.m_mmap.retaddr;
}

int minix_vfs_mmap(endpoint_t who, off_t offset, size_t len,
	dev_t dev, ino_t ino, int fd, vir_bytes vaddr, u16_t clearend,
	u16_t flags)
{
	message m;

	memset(&m, 0, sizeof(message));

	m.m_vm_vfs_mmap.who = who;
	m.m_vm_vfs_mmap.offset = offset;
	m.m_vm_vfs_mmap.dev = dev;
	m.m_vm_vfs_mmap.ino = ino;
	m.m_vm_vfs_mmap.vaddr = vaddr;
	m.m_vm_vfs_mmap.len = len;
	m.m_vm_vfs_mmap.fd = fd;
	m.m_vm_vfs_mmap.clearend = clearend;
	m.m_vm_vfs_mmap.flags = flags;

	return _syscall(VM_PROC_NR, VM_VFS_MMAP, &m);
}

/*
 * POSIX shm hook.  If fd is a registered shm object (shm_open), the IPC server
 * vm_remaps its shared region into us; returns MAP_FAILED for any other fd so
 * mmap() falls through.  Kept here (not in shm_open.c) and dependency-light so
 * the receiver of a passed shm fd works without calling shm_open(), and so the
 * dynamic linker's minimal libc link does not drag in heavy objects.
 */
static void *
__minix_shm_mmap(int fd, size_t len, int prot)
{
	message m;
	struct stat st;
	endpoint_t pt;

	if (fstat(fd, &st) < 0)
		return MAP_FAILED;
	if (minix_rs_lookup("ipc", &pt) != 0)
		return MAP_FAILED;
	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shm.dev = st.st_dev;
	m.m_lc_ipc_shm.ino = st.st_ino;
	m.m_lc_ipc_shm.size = len;
	m.m_lc_ipc_shm.flag = !(prot & PROT_WRITE);
	if (_syscall(pt, IPC_SHM_MAP, &m) != OK)
		return MAP_FAILED;
	return m.m_lc_ipc_shm.retaddr;
}

/* /dev/zero, which VM cannot map as a file: an anonymous mapping, as
 * elsewhere. */
static int
is_dev_zero(int fd)
{
	struct stat st;

	return fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) &&
	    major(st.st_rdev) == MEMORY_MAJOR && minor(st.st_rdev) == ZERO_DEV;
}

/* What POSIX has mmap() check; VM's own callers do not come this way.  As in
 * the servers, E* are negative here.  A file mapping with neither MAP_SHARED
 * nor MAP_PRIVATE (a bare MAP_FILE) is still taken as private, as NetBSD and
 * older software expect. */
static int
mmap_check(void *addr, int flags, int fd, off_t offset)
{

	if ((flags & (MAP_SHARED | MAP_PRIVATE)) == (MAP_SHARED | MAP_PRIVATE) ||
	    ((flags & MAP_FIXED) && ((vir_bytes)addr % PAGE_SIZE) != 0))
		return EINVAL;
	if (!(flags & MAP_ANON)) {
		if (fd < 0)
			return EBADF;
		if (offset % PAGE_SIZE != 0)
			return EINVAL;
	}
	return OK;
}

void *mmap(void *addr, size_t len, int prot, int flags,
	int fd, off_t offset)
{
	void *r;
	int e;

	if ((e = mmap_check(addr, flags, fd, offset)) != OK) {
		errno = -e;
		return MAP_FAILED;
	}

	/*
	 * A shared, file-backed mapping of a POSIX shm object (shm_open) cannot
	 * use the normal file-mmap path, which copy-on-writes into private
	 * pages.  Route it to the IPC server, which vm_remaps the shared region.
	 * __minix_shm_mmap() returns MAP_FAILED for any non-shm fd, so ordinary
	 * mappings fall through unchanged.
	 */
	if (fd >= 0 && (flags & MAP_SHARED) && !(flags & MAP_ANON)) {
		r = __minix_shm_mmap(fd, len, prot);
		if (r != MAP_FAILED)
			return r;
	}
	r = minix_mmap_for(SELF, addr, len, prot, flags, fd, offset);
	if (r == MAP_FAILED && errno == -ENODEV && !(flags & MAP_ANON) &&
	    is_dev_zero(fd))
		r = minix_mmap_for(SELF, addr, len, prot, flags | MAP_ANON, -1,
		    0);
	return r;
}

int munmap(void *addr, size_t len)
{
	message m;

	if (len == 0 || ((vir_bytes)addr % PAGE_SIZE) != 0) {
		errno = -EINVAL;
		return -1;
	}
	memset(&m, 0, sizeof(m));
	m.VMUM_ADDR = addr;
	m.VMUM_LEN = len;

	return _syscall(VM_PROC_NR, VM_MUNMAP, &m);
}


void *vm_remap(endpoint_t d,
			endpoint_t s,
			void *da,
			void *sa,
			size_t size)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_vmremap.destination = d;
	m.m_lsys_vm_vmremap.source = s;
	m.m_lsys_vm_vmremap.dest_addr = da;
	m.m_lsys_vm_vmremap.src_addr = sa;
	m.m_lsys_vm_vmremap.size = size;

	r = _syscall(VM_PROC_NR, VM_REMAP, &m);
	if (r != OK)
		return MAP_FAILED;
	return m.m_lsys_vm_vmremap.ret_addr;
}

void *vm_remap_ro(endpoint_t d,
			endpoint_t s,
			void *da,
			void *sa,
			size_t size)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_vmremap.destination = d;
	m.m_lsys_vm_vmremap.source = s;
	m.m_lsys_vm_vmremap.dest_addr = da;
	m.m_lsys_vm_vmremap.src_addr = sa;
	m.m_lsys_vm_vmremap.size = size;

	r = _syscall(VM_PROC_NR, VM_REMAP_RO, &m);
	if (r != OK)
		return MAP_FAILED;
	return m.m_lsys_vm_vmremap.ret_addr;
}

int vm_unmap(endpoint_t endpt, void *addr)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_shm_unmap.forwhom = endpt;
	m.m_lc_vm_shm_unmap.addr = addr;

	return _syscall(VM_PROC_NR, VM_SHM_UNMAP, &m);
}

unsigned long vm_getphys(endpoint_t endpt, void *addr)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lc_vm_getphys.endpt = endpt;
	m.m_lc_vm_getphys.addr = addr;

	r = _syscall(VM_PROC_NR, VM_GETPHYS, &m);
	if (r != OK)
		return 0;
	return (unsigned long) m.m_lc_vm_getphys.ret_addr;
}

u8_t vm_getrefcount(endpoint_t endpt, void *addr)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_vm_getref.endpt = endpt;
	m.m_lsys_vm_getref.addr  = addr;

	r = _syscall(VM_PROC_NR, VM_GETREF, &m);
	if (r != OK)
		return (u8_t) -1;
	return (u8_t) m.m_lsys_vm_getref.retc;
}

