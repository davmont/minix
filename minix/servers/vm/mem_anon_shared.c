/* This file implements the methods of shared anonymous memory: what
 * mmap(MAP_SHARED | MAP_ANON) gives.
 *
 * Unlike anonymous memory, a fork does not copy it on write: parent and child
 * see the same pages, as POSIX requires.  The regions that map one such
 * object -- the one mmap() made, its copies in children, and the parts a
 * split left -- are kept on a ring, each with its offset into the object.
 * The page a fault brings in for one of them goes into every region of the
 * ring that covers it, so that it lives on while any of them does: a child
 * may write to a page its parent had never touched, and exit.  Thus for any
 * offset either all regions covering it have the same page, or none does.
 *
 * Its pages are neither compressed nor swapped out: reclaim takes only
 * anonymous memory (mem_type_anon).
 */

#include <assert.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"

/* These functions are static so as to not pollute the
 * global namespace, and are accessed through their function
 * pointers.
 */

static int anon_shared_new(struct vir_region *vr);
static void anon_shared_delete(struct vir_region *vr);
static int anon_shared_copy(struct vir_region *vr, struct vir_region *newvr);
static void anon_shared_split(struct vmproc *vmp, struct vir_region *vr,
			struct vir_region *r1, struct vir_region *r2);
static int anon_shared_lowshrink(struct vir_region *vr, vir_bytes len);
static int anon_shared_unreference(struct phys_region *pr);
static int anon_shared_pagefault(struct vmproc *vmp,
	struct vir_region *region, struct phys_region *ph, int write,
	vfs_callback_t cb, void *state, int len, int *io);
static int anon_shared_sanitycheck(struct phys_region *pr, const char *file,
	int line);
static int anon_shared_writable(struct phys_region *pr);
static int anon_shared_pt_flags(struct vir_region *vr);

struct mem_type mem_type_anon_shared = {
	.name = "shared anonymous memory",
	.ev_new = anon_shared_new,
	.ev_delete = anon_shared_delete,
	.ev_copy = anon_shared_copy,
	.ev_split = anon_shared_split,
	.ev_lowshrink = anon_shared_lowshrink,
	.ev_unreference = anon_shared_unreference,
	.ev_pagefault = anon_shared_pagefault,
	.ev_sanitycheck = anon_shared_sanitycheck,
	.writable = anon_shared_writable,
	.pt_flags = anon_shared_pt_flags,
};

static int anon_shared_pt_flags(struct vir_region *vr){
#if defined(__arm__)
	return ARM_VM_PTE_CACHED;
#else
	return 0;
#endif
}

/* Put 'vr' on the ring after 'at'. */
static void ring_insert(struct vir_region *at, struct vir_region *vr)
{
	struct vir_region *next = at->param.anon_shared.next;

	USE(vr,
		vr->param.anon_shared.prev = at;
		vr->param.anon_shared.next = next;);
	USE(next, next->param.anon_shared.prev = vr;);
	USE(at, at->param.anon_shared.next = vr;);
}

static int anon_shared_new(struct vir_region *vr)
{
	USE(vr,
		vr->param.anon_shared.next = vr->param.anon_shared.prev = vr;
		vr->param.anon_shared.offset = 0;);

	return OK;
}

static void anon_shared_delete(struct vir_region *vr)
{
	struct vir_region *next = vr->param.anon_shared.next,
		*prev = vr->param.anon_shared.prev;

	/* Never on the ring: a split or copy that failed half-way. */
	if(!next)
		return;

	USE(next, next->param.anon_shared.prev = prev;);
	USE(prev, prev->param.anon_shared.next = next;);
	USE(vr, vr->param.anon_shared.next = vr->param.anon_shared.prev = NULL;);
}

static int anon_shared_copy(struct vir_region *vr, struct vir_region *newvr)
{
	/* map_copy_region() then gives the copy all of vr's pages. */
	USE(newvr, newvr->param.anon_shared.offset =
		vr->param.anon_shared.offset;);
	ring_insert(vr, newvr);

	return OK;
}

static void anon_shared_split(struct vmproc *vmp, struct vir_region *vr,
			struct vir_region *r1, struct vir_region *r2)
{
	assert(r1->length + r2->length == vr->length);

	USE(r1, r1->param.anon_shared.offset = vr->param.anon_shared.offset;);
	USE(r2, r2->param.anon_shared.offset =
		vr->param.anon_shared.offset + r1->length;);
	ring_insert(vr, r1);
	ring_insert(r1, r2);
	/* vr itself leaves the ring when it is freed. */
}

static int anon_shared_lowshrink(struct vir_region *vr, vir_bytes len)
{
	USE(vr, vr->param.anon_shared.offset += len;);

	return OK;
}

static int anon_shared_unreference(struct phys_region *pr)
{
	return mem_type_anon.ev_unreference(pr);
}

/* Take page 'pb' out of the regions of the ring other than 'region' that
 * have it at 'objoff' in the object.
 */
static void ring_unreference(struct vir_region *region,
	struct phys_block *pb, vir_bytes objoff)
{
	struct vir_region *vr;
	struct phys_region *pr;

	for(vr = region->param.anon_shared.next; vr != region;
	    vr = vr->param.anon_shared.next) {
		if(objoff < vr->param.anon_shared.offset ||
		    objoff >= vr->param.anon_shared.offset + vr->length)
			continue;
		pr = physblock_get(vr, objoff - vr->param.anon_shared.offset);
		if(!pr || pr->ph != pb)
			continue;
		pb_unreferenced(vr, pr, 1);
		SLABFREE(pr);
	}
}

static int anon_shared_pagefault(struct vmproc *vmp,
	struct vir_region *region, struct phys_region *ph, int write,
	vfs_callback_t cb, void *state, int len, int *io)
{
	struct vir_region *vr;
	vir_bytes objoff = region->param.anon_shared.offset + ph->offset;
	phys_bytes new_page_cl;
	u32_t allocflags;

	/* Shared, the page is writable as it is: never a copy on write. */
	if(ph->ph->phys != MAP_NONE)
		return OK;

	/* A new page: no other region on the ring has it (see above). */
	assert(ph->ph->refcount == 1);

	/* As for anonymous memory, the OOM reserve stays for the services. */
	allocflags = vrallocflags(region->flags);
	if(acl_is_user_proc(vmp) || (region->flags & VR_USERMEM))
		allocflags |= PAF_USERMEM;

	if((new_page_cl = alloc_mem(1, allocflags)) == NO_MEM) {
		printf("anon_shared_pagefault: out of memory\n");
		return ENOMEM;
	}
	USE(ph->ph, ph->ph->phys = CLICK2ABS(new_page_cl););

	/* Every other region of the ring that covers the page gets it too; its
	 * page table entry is written when it faults on the page.
	 */
	for(vr = region->param.anon_shared.next; vr != region;
	    vr = vr->param.anon_shared.next) {
		vir_bytes o;

		if(objoff < vr->param.anon_shared.offset ||
		    objoff >= vr->param.anon_shared.offset + vr->length)
			continue;
		o = objoff - vr->param.anon_shared.offset;
		assert(!physblock_get(vr, o));
		if(!pb_reference(ph->ph, o, vr, &mem_type_anon_shared)) {
			/* All or nothing: map_pf() drops the page from
			 * 'region', which frees it. */
			ring_unreference(region, ph->ph, objoff);
			return ENOMEM;
		}
	}

	return OK;
}

static int anon_shared_sanitycheck(struct phys_region *pr, const char *file,
	int line)
{
	MYASSERT(usedpages_add(pr->ph->phys, VM_PAGE_SIZE) == OK);
	return OK;
}

static int anon_shared_writable(struct phys_region *pr)
{
	assert(pr->ph->refcount > 0);
	return pr->ph->phys != MAP_NONE;
}
