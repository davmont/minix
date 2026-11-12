#define _SYSTEM 1

/* mlock(2), munlock(2), mlockall(2) and munlockall(2).
 *
 * A locked region has VR_LOCKED.  Locking brings its pages in, and reclaim
 * leaves them be: it neither compresses (and so swaps out) nor evicts them.
 * Bringing in file pages may need VFS, so locking may suspend the caller and
 * finish in a callback, as msync(2) does.
 *
 * A process may lock up to its RLIMIT_MEMLOCK, the superuser any amount; PM
 * tells VM which processes are the superuser's (vm_memlock_priv).  The locks
 * are the address space's: a thread's call acts on its group leader's, a
 * fork() child has none, and they go with the address space at exec.
 *
 * A call marks the regions it locks VR_MLOCKING until their pages are in; if
 * it fails, those are unlocked again, and what was locked before stays so.
 * After mlockall(MCL_FUTURE), a new mapping and the heap's growth are locked
 * and brought in too: see mlock_mmap_populate() and mlock_brk_populate().
 */

#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ds.h>
#include <minix/endpoint.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/bitmap.h>

#include <machine/vmparam.h>

#include <sys/mman.h>
#include <sys/param.h>

#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "glo.h"
#include "proto.h"
#include "util.h"
#include "region.h"

struct mlock_state {
	endpoint_t caller;		/* who called, to reply to */
	vir_bytes start, limit;		/* the range locked */
	vir_bytes next;			/* the next page to bring in */
	int mmap;			/* for mmap(2): reply with its address */
	int sync;			/* may not suspend: skip file pages */
	int retry;			/* VFS was asked for the next page */
};

static void mlock_cont(struct vmproc *vmp, message *replymsg, void *cbarg,
	void *statearg);

/* Would locking 'add' more bytes take the process past its RLIMIT_MEMLOCK? */
int map_memlock_exceeded(struct vmproc *vmp, vir_bytes add)
{
	u64_t total;

	if(vmp->vm_memlock_priv)
		return 0;
	total = (u64_t) map_locked_bytes(vmp, 0, (vir_bytes) -1, 1) + add;
	return total > vmp->vm_memlock_cur;
}

/* Bring in the pages of the locked regions in [next, limit).  A page that
 * cannot be had (of a file mapping past the end of its file) is left out:
 * it would be a SIGBUS anyway.  Out of memory, the call fails (EAGAIN).
 */
static int mlock_step(struct vmproc *vmp, struct mlock_state *st)
{
	struct vir_region *vr;
	region_iter iter;
	vir_bytes offset;
	int r, wr;

	while(st->next < st->limit) {
		if(!(vr = map_lookup(vmp, st->next, NULL))) {
			/* A hole (mlockall(), or unmapped meanwhile by a
			 * thread): on to the next region. */
			region_start_iter(&vmp->vm_regions_avl, &iter,
			    st->next, AVL_GREATER_EQUAL);
			if(!(vr = region_get_iter(&iter)))
				break;
			st->next = vr->vaddr;
			st->retry = 0;
			continue;
		}
		/* Nothing to bring in: not (or no longer) locked, no access,
		 * or memory VM does not manage. */
		if(!(vr->flags & VR_LOCKED) ||
		    (vr->flags & (VR_NOACCESS | VR_DIRECT))) {
			st->next = vr->vaddr + vr->length;
			st->retry = 0;
			continue;
		}

		offset = st->next - vr->vaddr;
		/* Private writable memory is brought in written to, so that a
		 * later write needs no copy; a shared file mapping only read,
		 * so as not to dirty its file. */
		wr = (vr->flags & VR_WRITABLE) && !(vr->flags & VR_SHARED);

		if(vr->def_memtype == &mem_type_mappedfile && st->sync) {
			st->next = vr->vaddr + vr->length;
			continue;
		}
		if(vr->def_memtype == &mem_type_mappedfile && !st->retry) {
			st->retry = 1;	/* in the copy the callback gets */
			r = map_handle_memory(vmp, vr, offset, VM_PAGE_SIZE,
			    wr, mlock_cont, st, sizeof(*st));
			if(r == SUSPEND)
				return SUSPEND;
		} else {
			/* The second time round for a file page, VFS has
			 * been asked: as handle_memory_step(), do not ask
			 * again. */
			r = map_handle_memory(vmp, vr, offset, VM_PAGE_SIZE,
			    wr, NULL, NULL, 0);
			assert(r != SUSPEND);
		}
		st->retry = 0;

		if(r == ENOMEM)
			return EAGAIN;
		st->next += VM_PAGE_SIZE;
	}

	return OK;
}

/* Reply to the caller, once the pages are in (or the call failed).  An
 * mlock() that fails unlocks what it locked; an mmap() stands, as Linux has
 * it, with what could be brought in.
 */
static void mlock_reply(struct vmproc *vmp, struct mlock_state *st, int r)
{
	message reply;

	memset(&reply, 0, sizeof(reply));
	if(st->mmap) {
		reply.m_type = OK;
		reply.m_mmap.retaddr = (void *) st->start;
	} else {
		map_lock_done(vmp, st->start, st->limit, r != OK);
		reply.m_type = r;
	}
	if(ipc_send(st->caller, &reply) != OK)
		printf("VM: mlock_reply: ipc_send() failed\n");
}

static void mlock_cont(struct vmproc *vmp, message *replymsg, void *cbarg,
	void *statearg)
{
	struct mlock_state *st = statearg;
	int r;

	if(vmp == NULL)
		return;				/* the process is gone */

	/* VFS could not have the page: leave it out (see mlock_step()). */
	if(replymsg->VMV_RESULT != OK) {
		st->next += VM_PAGE_SIZE;
		st->retry = 0;
	}

	if((r = mlock_step(vmp, st)) == SUSPEND)
		return;
	mlock_reply(vmp, st, r);
}

/* A file mapping made after mlockall(MCL_FUTURE) is locked (mmap_region()):
 * bring its pages in before mmap() returns.  SUSPEND if VFS was asked; the
 * reply, with the address, then comes from mlock_cont().
 */
int mlock_mmap_populate(struct vmproc *vmp, endpoint_t caller, vir_bytes addr,
	vir_bytes len)
{
	struct mlock_state st;

	memset(&st, 0, sizeof(st));
	st.caller = caller;
	st.mmap = 1;
	st.start = st.next = addr;
	st.limit = addr + roundup(len, VM_PAGE_SIZE);
	return mlock_step(vmp, &st) == SUSPEND ? SUSPEND : OK;
}

/* The heap grew to 'limit' from 'start', locked: lock what is new and bring
 * it in.  The heap is anonymous memory, so this does not suspend; out of
 * memory, the pages stay locked and come in at the fault, as Linux has it.
 */
void mlock_brk_populate(struct vmproc *vmp, vir_bytes start, vir_bytes limit)
{
	struct mlock_state st;

	/* Only when the region count is full may this fail: then the growth
	 * is just not locked. */
	if(map_lock_range(vmp, start, limit - start, 1) != OK)
		return;
	memset(&st, 0, sizeof(st));
	st.sync = 1;
	st.start = st.next = start;
	st.limit = limit;
	(void) mlock_step(vmp, &st);
	map_lock_done(vmp, start, limit, 0);
}

/*===========================================================================*
 *				do_mlock				     *
 *===========================================================================*/
int do_mlock(message *m)
{
	struct mlock_state st;
	struct vmproc *vmp;
	vir_bytes addr, len;
	int n, op, flags, r;

	if((r = vm_isokendpt(m->m_source, &n)) != OK)
		panic("do_mlock: message from strange source: %d",
			m->m_source);
	vmp = vm_lwp_group(&vmproc[n]);

	op = m->m_lc_vm_mlock.op;
	addr = (vir_bytes) m->m_lc_vm_mlock.addr;
	len = m->m_lc_vm_mlock.len;
	flags = m->m_lc_vm_mlock.flags;

	memset(&st, 0, sizeof(st));
	st.caller = m->m_source;

	switch(op) {
	case VMML_LOCK:
	case VMML_UNLOCK:
		/* Unprivileged, with no RLIMIT_MEMLOCK at all: EPERM, as
		 * Linux has it, before anything else. */
		if(op == VMML_LOCK && !vmp->vm_memlock_priv &&
		    vmp->vm_memlock_cur == 0)
			return EPERM;
		/* The pages the range touches, as NetBSD and Linux have it:
		 * a buffer from malloc() need not start on a page.  (POSIX
		 * permits EINVAL instead.) */
		len += addr % VM_PAGE_SIZE;
		addr -= addr % VM_PAGE_SIZE;
		if(len == 0)
			return OK;
		if(len > (vir_bytes) -1 - (VM_PAGE_SIZE - 1) - addr)
			return ENOMEM;
		len = roundup(len, VM_PAGE_SIZE);

		if(op == VMML_UNLOCK)
			return map_lock_range(vmp, addr, len, 0);

		/* Check the range first: ENOMEM for a hole in it, whatever
		 * the limit. */
		if(map_locked_bytes(vmp, addr, addr + len, 0) +
		    map_locked_bytes(vmp, addr, addr + len, 1) != len)
			return ENOMEM;
		if(map_memlock_exceeded(vmp,
		    map_locked_bytes(vmp, addr, addr + len, 0)))
			return ENOMEM;
		if((r = map_lock_range(vmp, addr, len, 1)) != OK)
			return r;
		st.start = st.next = addr;
		st.limit = addr + len;
		break;

	case VMML_LOCKALL:
		if(flags == 0 || (flags & ~(MCL_CURRENT | MCL_FUTURE)))
			return EINVAL;
		if(!vmp->vm_memlock_priv && vmp->vm_memlock_cur == 0)
			return EPERM;
		if(flags & MCL_FUTURE)
			vmp->vm_flags |= VMF_MCL_FUTURE;
		if(!(flags & MCL_CURRENT))
			return OK;
		if(map_memlock_exceeded(vmp,
		    map_locked_bytes(vmp, 0, (vir_bytes) -1, 0)))
			return ENOMEM;
		map_lock_all(vmp);
		st.start = st.next = 0;
		st.limit = (vir_bytes) -1;
		break;

	case VMML_UNLOCKALL:
		vmp->vm_flags &= ~VMF_MCL_FUTURE;
		map_unlock_all(vmp);
		return OK;

	default:
		return EINVAL;
	}

	if((r = mlock_step(vmp, &st)) == SUSPEND)
		return SUSPEND;
	map_lock_done(vmp, st.start, st.limit, r != OK);
	return r;
}
