
/* This file implements the methods of anonymous memory.
 * 
 * Anonymous memory is memory that is for private use to a process
 * and can not be related to a file (hence anonymous).
 */

#include <assert.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"
#include "lz4.h"

/* These functions are static so as to not pollute the
 * global namespace, and are accessed through their function
 * pointers.
 */

static void anon_split(struct vmproc *vmp, struct vir_region *vr,
			struct vir_region *r1, struct vir_region *r2);
static int anon_lowshrink(struct vir_region *vr, vir_bytes len);
static int anon_unreference(struct phys_region *pr);
static int anon_pagefault(struct vmproc *vmp, struct vir_region *region, 
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io);
static int anon_sanitycheck(struct phys_region *pr, const char *file, int line);
static int anon_writable(struct phys_region *pr);
static int anon_resize(struct vmproc *vmp, struct vir_region *vr, vir_bytes l);
static u32_t anon_regionid(struct vir_region *region);
static int anon_refcount(struct vir_region *vr);
static int anon_pt_flags(struct vir_region *vr);

struct mem_type mem_type_anon = {
	.name = "anonymous memory",
	.ev_unreference = anon_unreference,
	.ev_pagefault = anon_pagefault,
	.ev_resize = anon_resize,
	.ev_sanitycheck = anon_sanitycheck,
	.ev_lowshrink = anon_lowshrink,
	.ev_split = anon_split,
	.regionid = anon_regionid,
	.writable = anon_writable,
	.refcount = anon_refcount,
	.pt_flags = anon_pt_flags,
};

static int anon_pt_flags(struct vir_region *vr){
#if defined(__arm__)
	return ARM_VM_PTE_CACHED;
#else
	return 0;
#endif
}

static int anon_unreference(struct phys_region *pr)
{
	assert(pr->ph->refcount == 0);
	if(pr->ph->phys != MAP_NONE)
		free_mem(ABS2CLICK(pr->ph->phys), 1);
	else if(pr->ph->flags & PBF_COMPRESSED) {
		/* Contents live in the store; the process is going away
		 * (exit/unmap), so just drop the blob - from disk (phase C)
		 * or RAM.  For a RAM blob, cancel any in-flight write-back of
		 * it first so its completion does not touch the freed slot. */
		if(swapstore_handle_is_disk(pr->ph->pb_zref)) {
			swapstore_slot_free(
				swapstore_handle_to_slot(pr->ph->pb_zref));
		} else {
			swapout_cancel_pb(pr->ph);
			zstore_free(pr->ph->pb_zref);
		}
		USE(pr->ph,
			pr->ph->flags &= ~PBF_COMPRESSED;
			pr->ph->pb_zref = NULL;);
	}
	return OK;
}

static int anon_pagefault(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io)
{
	phys_bytes new_page, new_page_cl;
	u32_t allocflags;

	allocflags = vrallocflags(region->flags);

	assert(ph->ph->refcount > 0);

	if((new_page_cl = alloc_mem(1, allocflags)) == NO_MEM) {
		printf("anon_pagefault: out of memory\n");
		return ENOMEM;
	}
	new_page = CLICK2ABS(new_page_cl);

	/* Totally new block? Create it - either by zero-fill (the page
	 * never existed) or by decompressing it back from the zstore
	 * (it was compressed out under memory pressure; phase B).
	 */
	if(ph->ph->phys == MAP_NONE) {
		if(ph->ph->flags & PBF_COMPRESSED) {
			/* On the swap disk (phase C3)?  Read it back
			 * asynchronously: suspend the fault now and resume it
			 * from the swapio completion.  A synchronous caller
			 * (cb == NULL, e.g. the fault-retry) cannot suspend -
			 * but after a read-in the page is resident, so the
			 * retry never reaches here. */
			if(swapstore_handle_is_disk(ph->ph->pb_zref)) {
				if(cb == NULL) {
					free_mem(new_page_cl, 1);
					printf("anon_pagefault: swap-in needs "
						"async callback\n");
					return EFAULT;
				}
				return anon_swapin_start(vmp, region, ph, write,
					cb, state, len, io, new_page,
					new_page_cl);
			}

			/* RAM blob: cancel any in-flight write-back of this
			 * page, then decompress straight into the freshly
			 * allocated frame via sys_abscopy (inside
			 * zstore_get_phys) - no per-fault VM mapping or
			 * TLB flush. */
			swapout_cancel_pb(ph->ph);
			zstore_get_phys(ph->ph->pb_zref, new_page);
			USE(ph->ph,
				ph->ph->flags &= ~PBF_COMPRESSED;
				ph->ph->pb_zref = NULL;);
			ph->ph->phys = new_page;

			/* A page compressed before a fork is shared
			 * (refcount > 1) afterwards.  Unlike a fresh
			 * zero-fill block (always refcount 1 here), a
			 * write fault must therefore still COW: our
			 * caller expects the page to be writable on
			 * return when 'write' is set - there is no
			 * hardware retry for VM-internal
			 * handle_memory() users such as fork's message
			 * materialization, so returning a read-only
			 * shared page would fault forever.
			 */
			if(ph->ph->refcount > 1 && write) {
				assert(region->flags & VR_WRITABLE);
				return mem_cow(region, ph, MAP_NONE,
					MAP_NONE);
			}

			return OK;
		}
		ph->ph->phys = new_page;
		assert(ph->ph->phys != MAP_NONE);

		return OK;
	}

	if(ph->ph->refcount < 2 || !write) {
		/* memory is ready already */
		return OK;
	}	

        assert(region->flags & VR_WRITABLE);

	return mem_cow(region, ph, new_page_cl, new_page);
}

static int anon_sanitycheck(struct phys_region *pr, const char *file, int line)
{
	MYASSERT(usedpages_add(pr->ph->phys, VM_PAGE_SIZE) == OK);
	return OK;
}

static int anon_writable(struct phys_region *pr)
{
	assert(pr->ph->refcount > 0);
	if(pr->ph->phys == MAP_NONE)
		return 0;
	if(pr->parent->remaps > 0)
		return 1;
	return pr->ph->refcount == 1;
}

static int anon_resize(struct vmproc *vmp, struct vir_region *vr, vir_bytes l)
{
	/* Shrinking not implemented; silently ignored.
	 * (Which is ok for brk().)
	 */
	if(l <= vr->length)
		return OK;

        assert(vr);
        assert(vr->flags & VR_ANON);
        assert(!(l % VM_PAGE_SIZE));

        USE(vr, vr->length = l;);

	return OK;
}

static u32_t anon_regionid(struct vir_region *region)
{
	return region->id;
}

static int anon_lowshrink(struct vir_region *vr, vir_bytes len)
{
	return OK;
}

static int anon_refcount(struct vir_region *vr)
{
        return 1 + vr->remaps;
}

static void anon_split(struct vmproc *vmp, struct vir_region *vr,
			struct vir_region *r1, struct vir_region *r2)
{
	return;
}
