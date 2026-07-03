# VM page reclaim — Phase A: clean-page reclaim

Status: **A0 (instrumentation) and A2 (proactive batched reclaim)
implemented** — see "A0 results" and "A2 results" below. A1 (eviction
of mapped clean file pages) not yet implemented; A0's data reordered
the phases to A2 -> A1 -> A3.
Track: memory management (swap/paging). Phase A of three:
  A. clean-page reclaim (this doc)
  B. compressed anonymous memory (zram-style)
  C. disk swap

## Problem

MINIX has no page replacement. Memory is demand-*allocated* but never
*evicted*: when free memory runs out, allocations fail (`ENOMEM`, or
`SIGSEGV` on a page fault that cannot be satisfied). Consequences:

- Nothing larger than RAM can run; no graceful degradation under pressure.
- The FS buffer cache cannot grow into free memory safely, because there
  is no way to shrink it *or* anything else when a process needs pages.
- Program text of running processes is pinned for the process lifetime,
  even though it is clean and trivially re-fetchable.

## What already exists (survey, 2026-07-02)

The current tree is much closer to reclaim than expected. Inventory:

### 1. VM block cache with LRU and a working reclaimer
`servers/vm/cache.c`:
- Hash by `(dev, dev_offset)` and `(dev, ino, ino_offset)`;
  `struct cached_page` entries, global LRU list, `cached_pages` counter.
- **`cache_freepages(int pages)` (cache.c:288)** walks the LRU from the
  oldest end and frees pages — but *only* those with
  `pb->refcount == 1`, i.e. pages referenced by the cache alone.
- Trigger (`alloc.c:276-278`): `alloc_mem()` retries after
  `cache_freepages(clicks)` **only on hard allocation failure**
  (`NO_MEM`). There are no watermarks and no proactive reclaim.

### 2. FS buffer cache (lmfs) is VM-backed
`lib/libminixfs/cache.c`: an FS server's buffers are VM cache pages:
- `vm_map_cacheblock()` (cache.c:518) maps an existing VM-cached block
  into the FS's address space; `vm_set_cacheblock()` (cache.c:641)
  publishes a block into the VM cache; `vm_forget_cacheblock()` /
  `vm_clear_cache()` remove entries.
- While mapped by the FS these pages are `mem_type_cache` regions
  (`mem_cache.c`) — **writable, possibly dirty**. When lmfs evicts a
  buffer it `munmap`s it (cache.c:331, 910, 1372); the page then remains
  in the VM cache with `refcount == 1` — i.e. today's reclaimable pool.

### 3. File-mapped pages are clean by construction and re-fetchable
`servers/vm/mem_file.c` (`mem_type_mappedfile` — user `mmap()` of files
*and* program text, since exec maps executables via the FS peek
protocol):
- `mappedfile_writable()` returns 0 — these mappings are **never
  writable**. A write fault COWs the page to `mem_type_anon`
  (`cow_block()`), so a mappedfile page can never hold dirty data.
- `mappedfile_pagefault()` (mem_file.c:84) already re-fetches a missing
  page (`ph->ph->phys == MAP_NONE`): first from the VM cache
  (`find_cached_page_bydev/byino`), else via a VFS round-trip
  (`vfs_request(VMVFSREQ_FDIO, ...)` → FS reads the block →
  `vm_set_cacheblock` → fault retried).
- `map_pf()` (region.c:664) regenerates a missing `phys_region` on
  fault (`pb_new(MAP_NONE)` + `pb_reference`), so a page eviction that
  removes the `phys_region` entirely is transparently undone by the
  next access.

### 4. Reverse mapping exists
`servers/vm/pb.c` / `region.h`: every `phys_block` keeps
`pb->firstregion`, a chain of all `phys_region`s (via `next_ph_list`)
that map it; each `phys_region` knows its `vir_region` (`->parent`) and
thereby the owning `vmproc`. Finding all mappers of a page is O(mappers).

## The gap

`cache_freepages()` skips every page with `refcount > 1`. On a running
system, most file pages are *mapped* (text of running programs, shared
libs would be too if dynamic linking mattered, mmap'ed files), so under
real pressure the reclaimable pool is nearly empty exactly when it is
needed. **Phase A = make clean *mapped* file pages evictable, and
reclaim them proactively.**

## Design

### A1. Page eviction for mapped clean file pages (the core)

New function in `vm/cache.c` (or `region.c`), roughly:

```
int evict_mapped_clean(struct cached_page *cp)
```

Preconditions checked:
- Every `phys_region` on `cp->page->firstregion` has
  `memtype == &mem_type_mappedfile`. If **any** referent is
  `mem_type_cache` (the FS holds it — possibly dirty) or anything else
  (anon after COW never shares this pb, but be defensive): **skip**.

Action, per referencing `phys_region pr`:
1. Clear the PTE in the owner's page table (same `pt_writemap` path
   used by existing unmap/shrink code; kernel handles cross-CPU TLB
   shootdown — see Risks).
2. `pb_unreferenced(region, pr, 1)` — drop the mapping and remove the
   phys_region from the region's physblock tree, exactly the inverse of
   what `map_pf()` recreates on the next fault.

After all mappers are detached, `refcount == 1` (cache only) and the
existing `rmcache()`/`cache_freepages()` machinery frees the page.

Next access by any evicted process → page fault → `map_pf()` →
`mappedfile_pagefault()` → VM-cache hit (if only some mappers were
evicted) or FS round-trip. **No new fault-side code is needed.**

### A2. Extend the LRU sweep

`cache_freepages()` grows a second pass (or a sibling
`reclaim_pages()`): walk LRU-oldest first; for entries with
`refcount > 1` whose extra references are all mappedfile mappers, call
`evict_mapped_clean()` and free. Bounded per call (see A3).

Coverage note: `VMSF_ONCE` pages (isofs/vfat exec paths) are removed
from the cache immediately after mapping, so they are invisible to an
LRU walk. Phase A accepts this (the root FS, MFS, does not use ONCE);
a later refinement can walk process regions as a fallback.

### A3. Proactive watermark trigger

Today reclaim runs only inside `alloc_mem()` on hard failure. Add:
- Low/high watermarks on free pages (e.g. low = max(1% of RAM, 2 MB),
  high = 2×low; tunable).
- Check on the allocation path and in `alloc_cycle()` (already invoked
  from the main loop when the allocator wants attention — main.c:119):
  when below low, reclaim up to high.
- **Bounded work per invocation** (e.g. ≤64 pages per cycle): VM is a
  single-threaded event loop; long sweeps inside one message would add
  latency to every VM client.
- Keep the existing hard-failure retry as the backstop.

### A4. Observability

- Counters: pages evicted (unmapped-clean vs cache-only), refaults
  served from cache vs from FS, reclaim invocations, watermark hits.
- Expose via `get_stats_info()` (`vsi_cached` already exists) so
  `vmstat`/`top` can show them.

## What Phase A explicitly does NOT do

- **No anonymous-memory eviction** — needs compression/swap (Phases B/C).
- **No dirty-page writeback from VM** — dirty data only ever lives in
  `mem_type_cache` pages held by FS servers, which manage their own
  writeback (lmfs flush). VM never initiates I/O for reclaim in Phase A.
- **No FS-side (lmfs primary cache) shrinking** — lmfs already returns
  pages to the reclaimable pool when it evicts buffers; tuning lmfs
  sizing under pressure is a possible A follow-up, not core.
- No thrash control beyond LRU order + bounded sweeps.

## Risks

1. **Cross-process PTE removal on SMP** (the #1 risk). Eviction clears
   PTEs of processes that may be running on another CPU. The kernel
   already has the machinery VM uses when modifying live page tables
   (`smp_schedule_vminhibit` / `RTS_VMINHIBIT`, `MF_FLUSH_TLB`), and
   `pt_writemap` is the existing entry point — but this path has not
   been exercised by *unsolicited* (not process-initiated) unmaps at
   scale. Must validate under `-smp 4/8` with the stress harness; watch
   for stale-TLB reads of freed pages.
2. **VM cache/lmfs protocol assumptions**: verify lmfs never assumes a
   published block stays in the VM cache (it must already tolerate loss
   — `cache_freepages` frees refcount==1 pages today — but the *mapped*
   eviction changes timing).
3. **Fault storms / livelock under extreme pressure**: evicting a page
   the process immediately refaults burns FS round-trips. LRU order
   mitigates; watermark hysteresis (low→high) prevents oscillation.
   Accepted for Phase A; measure.
4. **exec/peek dependence**: text eviction only helps for FSes with
   working mmap/peek (MFS, isofs, vfat have it). Static fallback exec
   (copy-in) pages are anon → untouched. Fine.

## Validation plan

QEMU harnesses (reuse the SMP A/B + stress patterns from the
runqueue-lock work):
1. **Pressure progression**: `-m 64/96/128`, run a growing memory hog +
   parallel workload. BASE devel: record where ENOMEM/OOM kills start.
   FIX: must progress strictly further / complete workloads BASE cannot.
2. **Correctness under eviction**: low RAM, many processes re-executing
   binaries and reading mmap'ed files in a loop; checksum outputs
   (catches text/data corruption from bad evictions). Run at `-smp 4`.
3. **Regression**: full boot + build workload at `-m 2048` — counters
   should show ~0 evictions; no performance change.
4. **Counters sanity**: evictions > 0 under pressure; refault-from-cache
   vs refault-from-FS ratio sane.

## Deliverables / phasing

- **A0 (small)**: counters + watermark plumbing only, no behavior
  change; quantify the pinned-vs-free pool under pressure. (1 build)
- **A1 (core)**: `evict_mapped_clean()` + LRU-sweep extension +
  hard-failure path uses it. (main work)
- **A2**: proactive watermark trigger, bounded per-cycle reclaim.
- **A3**: validation harness runs + fixes; PR with A/B numbers.

Estimated size: ~300-500 lines in `servers/vm/` (cache.c, alloc.c,
region.c/pb.c helper), no kernel changes expected (existing pt_writemap/
TLB machinery), no FS changes expected.

## A0 results (2026-07-02, QEMU -m 512 -smp 2, phased pressure run)

Phases: idle after boot → FS load (`cat` every file in /usr/bin,
/usr/lib, /bin, /sbin) → +30 background processes → 60 MB anonymous hog
(`yes | head -c 60M | sort`). `/proc/meminfo` sampled after each:

| phase   | free   | cached | pinned | evictable | reclaim calls | freed pages | alloc fails |
|---------|--------|--------|--------|-----------|---------------|-------------|-------------|
| idle    | 416 MB |  54 MB |  45 MB |     0 MB  |             0 |           0 | 0 |
| FS load |   0.3 MB| 459 MB|  48 MB |    2.1 MB |        18,188 |      18,188 | 0 |
| +procs  |   0.7 MB| 457 MB|  48 MB |    2.3 MB |        18,844 |      18,844 | 0 |
| hog done|  77 MB | 382 MB |  38 MB |    2.3 MB |        37,982 |      37,982 | 0 |

No panics, no asserts (the `free_page_count` cross-check held), zero
allocation failures; one low-watermark episode.

**Findings — these reorder A1 vs A2:**

1. **The reactive valve works but is pathological.** After the cache
   fills RAM, *every* allocation takes the NO_MEM → `cache_freepages()`
   → retry path: ~38k reclaim invocations for one workload, each a
   just-enough LRU walk, with free memory pinned at ~0 the whole time
   (no headroom for bursts or multi-page contiguous requests).
   **A2 (proactive watermark reclaim, batched) is the immediate win**
   and is cheap: the watermark plumbing already exists from A0; A2 is
   essentially calling `cache_freepages(high - free)` at the crossing.
2. **The A1 evictable pool is small in typical MINIX workloads**
   (~2 MB): userland is statically linked (no shared libraries), so the
   mapped-clean-text pool is just the unique text of running binaries.
   The `pinned` 38-48 MB is almost entirely `mem_type_cache` — the FS's
   own mapped lmfs buffers — which reclaim must not touch.
   **A1 remains worthwhile for the big-binary case** (a native clang
   process maps ~167 MB of text/rodata; self-hosting builds are the
   flagship pressure workload), but it is no longer the first priority.

Revised order: **A2 → A1 → A3**, with A1 validated against a
self-hosting build (native clang) rather than a shell workload.

## A2 results (2026-07-02, same phased run as A0, QEMU -m 512 -smp 2)

A2 batches both reclaim paths: one proactive `cache_freepages()` sweep
toward the high watermark at the low-watermark crossing (one batch per
episode, hysteresis re-arms at the high mark, 16 MB/batch cap), and the
hard-failure retry in `alloc_mem()` now also frees a batch instead of
exactly the request size.

| phase   | free (A0 -> A2)   | reclaim calls (A0 -> A2) | freed pages (A0 -> A2) | alloc fails |
|---------|-------------------|--------------------------|------------------------|-------------|
| idle    | 416 MB -> 416 MB  |      0 ->  0             |      0 ->      0       | 0 |
| FS load | 0.3 MB -> 6 MB    | 18,188 -> 15             | 18,188 -> 19,650       | 0 |
| +procs  | 0.7 MB -> 9 MB    | 18,844 -> 16             | 18,844 -> 20,960       | 0 |
| hog done|  77 MB -> 82 MB   | 37,982 -> 30             | 37,982 -> 39,300       | 0 |

Reclaim invocations collapse ~1,266x (37,982 -> 30), each batch freeing
~1,310 pages (exactly the low->high span), and free memory now hovers
inside the watermark band (5-10 MB) under sustained pressure instead of
~0 -- so allocations stop paying failed full-bitmap scans, and headroom
exists for bursts and multi-page requests.  Zero allocation failures,
no asserts.  Episode accounting: 30 low-watermark episodes, one batch
each.
## A1 results (2026-07-02)

A1 adds `map_evict_clean_page()` (region.c) and a second, last-resort
pass in `cache_freepages()`: when freeing unmapped cache pages did not
satisfy the request AND the caller allows it (only the hard
allocation-failure retry in `alloc_mem()` does; proactive watermark
batches do not), clean file-mapped pages are evicted - every mapper's
PTE is cleared (`pt_writemap(MAP_NONE)`, which stops the target process
and marks it for a TLB flush via the kernel's VMINHIBIT/MF_FLUSH_TLB
machinery) and the phys_regions are detached; the next access re-faults
through the pre-existing `mappedfile_pagefault()` re-fetch path.

Safety predicates (any failure disqualifies the page, EBUSY):
- every mapper is `mem_type_mappedfile` (never writable => clean);
- every mapping process is a regular user process
  (`acl_is_user_proc()`): evicting from a system service could deadlock,
  as re-faulting needs VFS/FS/driver;
- no mapper is a thread-group leader (`vm_lwp_refcount > 0`): sibling
  threads on other CPUs could retain stale TLB entries, since the
  VMINHIBIT machinery only stops the process being modified;
- no mapper is exiting.

### Validation

**Deterministic trigger** (v11): a 400 MB `MAP_ANON|MAP_CONTIG|
MAP_PREALLOC` mmap (single whole-region `alloc_mem()`) on a 512 MB
machine. At fresh boot it succeeds (a contiguous run exists); after an
FS load fragments memory it cannot succeed, so the hard-failure retry
drains the cache (pass 1), **evicts 112 mapped clean text pages
(pass 2, `vsi_evicted` 0 -> 112)**, and fails with a clean ENOMEM
(`vsi_alloc_fails` 0 -> 1) - no SIGSEGV, no service damage.  Checksum
workers re-executing the evicted binaries throughout the run verify
**byte-identical re-fetch after eviction** (CK match), and the system
remains fully functional.  Same result at `-smp 2` and `-smp 4`
(cross-CPU VMINHIBIT/TLB path exercised).

**Sustained-pressure runs** (v5/v6/v8/v10, 96-112 MB, hours of
combined hog+worker load with the A1 code live): checksums always
matched, no panics, and eviction correctly did NOT engage while
cheaper reclaimable cache remained - confirming last-resort semantics.
Bounded single-process hogs cannot reach the eviction band because
per-process limits bind first; the deterministic contig trigger (or a
big-text workload, see A3) is needed to reach it.

**Pre-existing OOM behavior (A/B-confirmed, NOT an A1 regression):**
unbounded geometric anon demand (parallel awk string-doubling, 4x
~150 MB at 96 MB RAM) kills the system on BOTH the A1 image and the
no-A1 baseline with an identical chain: `anon_pagefault: out of
memory` -> VFS (anon fault, a pool reclaim never touches) SIGSEGV ->
core service died -> RS aborts -> kernel panic.  MINIX has no OOM
killer and no service memory reservation; that is a candidate follow-up
(fits Phase B/C).

Note on pool size: on MINIX's statically linked userland the evictable
text pool is small (~2 MB in these tests, 112 pages evicted = all of
it).  The realistic large-pool workload is a native-clang build
(~167 MB mapped text) - the planned A3 validation vehicle.
## A3 results (2026-07-02): native clang under reclaim

The realistic big-text workload: three consecutive native-clang compiles
(`/usr/bin/clang`, a 167 MB mapped binary; compile + link + run the
produced program) with concurrent FS load, a 30 MB anonymous hog, and
checksum workers, at decreasing RAM sizes on current devel (A0+A2+A1):

| RAM    | compiles | output OK | min free | reclaim activity        | evicted |
|--------|----------|-----------|----------|-------------------------|---------|
| 512 MB | 3/3      | yes       | 330 MB   | none                    | 0 |
| 256 MB | 3/3      | yes       |  71 MB   | none                    | 0 |
| 160 MB | 3/3      | yes       |   2 MB   | 18 calls, 9,234 pages (36 MB) | 0 |

Checksums byte-identical and system fully functional in every run; no
panics.  At 160 MB the proactive batched reclaim (A2) actively fed the
compile from the cache while the last-resort eviction (A1) correctly
remained unused - there was always cheaper cache to take first.

For perspective: before the reclaim work, the working guidance for
native clang was `-m 2048` ("tight" at 512 MB).  With phase A in place
the same compile pipeline completes comfortably at 256 MB and under
active memory pressure at 160 MB.  A1's eviction mechanism itself is
exercised deterministically by the MAP_CONTIG test above (112 pages,
byte-identical re-fetch, -smp 2/4); under organic workloads it is the
designed backstop between "cache empty" and "out of memory".

Phase A (A0 #290, A2 #291, A1 #292, A3 this section) is COMPLETE.
Next: Phase B (compressed anonymous memory), which also addresses the
A/B-confirmed total-OOM service-death gap (no OOM killer).


# Phase B: compressed anonymous memory (zram-style)

Status: **B0 (codec + self-test) in progress.** Design below.

## Problem

Phase A made file-backed memory reclaimable, but anonymous memory
(heap/stack) remains pinned: when anon demand exceeds RAM, faults fail
(`anon_pagefault: out of memory`) and - as A/B-demonstrated in the A1
work - a core service losing an anon fault takes the whole system down
(no OOM killer).  Phase B adds the modern first line of defense:
compress cold anonymous pages into a RAM pool (Linux zram/zswap, macOS
and Windows compressed memory), trading CPU for effective capacity
with no disk plumbing.

## Where it lives

Inside the VM server (not a separate service): VM owns all page state,
the compression is pure CPU work on pages VM can already map, and an
external server would add IPC latency to the fault path plus a
VM<->server memory-dependency cycle.  All work is bounded per
invocation (VM is a single-threaded event loop).

## Design

### Codec (B0)
Minimal LZ4 block-format codec (`servers/vm/lz4.c`), written for this
use: `vm_lz4_compress()` gives up early (returns 0) if output would
exceed a caller budget - incompressible pages are simply not stored -
and `vm_lz4_decompress()` is a bounds-checked safe decoder.  A boot
self-test round-trips zero/pattern/text-like/incompressible pages once
and prints the result; behavior is otherwise unchanged.

### Compressed store (B1)
- Pool pages come from `vm_allocpage()` (VM-mapped, so blobs are
  directly addressable); the pool grows on demand and is capped
  (tunable, default ~25% of total RAM).
- Blobs live in size-class slots (512/1024/2048 B) carved from pool
  pages, with per-class freelists.  A page is stored ONLY if it
  compresses to <= 2048 B, guaranteeing >= 2x net win per stored page
  (worst-case internal fragmentation still >= ~1.3x).  slaballoc()
  cannot serve blobs (max object 519 B).
- `struct phys_block` gains `PBF_COMPRESSED` + a blob reference
  (pointer + size).  Lifecycle: compress-out frees the physical page
  and stores the blob; `anon_pagefault` on a `PBF_COMPRESSED` block
  allocates a fresh page (no PAF_CLEAR) and decompresses into it;
  `anon_unreference` frees the blob if the process exits first.
  The `phys == MAP_NONE` + !PBF_COMPRESSED case remains zero-fill,
  exactly as today.

### Compress-out path (B1)
`cache_freepages()` gains a third, last-resort pass (after unmapped
cache and clean-file eviction): walk eligible anon pages and compress
them out until the request is satisfied or candidates run out.
Eligibility mirrors A1's safety predicates: `mem_type_anon`,
`refcount == 1` (no COW sharing), sole mapper is a regular user
process (`acl_is_user_proc`), not a thread-group leader, not exiting.
The mapper's PTE is cleared via the same `pt_writemap(MAP_NONE)` path
(VMINHIBIT + MF_FLUSH_TLB, cross-CPU safe).  Selection in B1 is a
bounded round-robin walk over process slots (no coldness tracking);
B2 can add accessed-bit scanning.

### Faults and kernel access
Every consumer of user memory reaches non-present pages through
`map_pf()`/`handle_memory()` (page faults, kernel delivermsg, grants,
sys_datacopy targets), which is exactly the path that today handles
never-materialized anon pages; decompress-in slots into
`anon_pagefault` with no new fault-side plumbing elsewhere.

### Observability
`vsi_zpages` (pages currently compressed), `vsi_zpool` (pool pages),
`vsi_zin/zout` (compress/decompress ops) appended to vm_stats_info +
/proc/meminfo (prefix-safe as before).

## Phasing
- **B0**: LZ4 codec + boot self-test; no behavior change.
- **B1**: store + compress-out pass + decompress-in fault + stats.
  Validation: anon-data integrity workload (write patterns, force
  compression, read back and verify); the A1 awk total-OOM workload
  must now push notably further before the (still pre-existing) OOM
  endgame; A2/A1 regression re-run.
- **B2**: coldness via accessed-bit scan; proactive compression below
  a deeper watermark; possibly blob packing (zsmalloc-style).
- **Beyond**: OOM-policy hardening (service memory reservation /
  userland OOM killer) and Phase C (disk swap behind the same store).

## Phase B1 status (WORK IN PROGRESS — not shippable, not merged)

The store (`zstore.c`), compress-out pass (`map_compress_anon_pages`
in `region.c`, gated as `cache_freepages()` pass three), decompress-in
(`anon_pagefault`), blob lifecycle (`anon_unreference`), the
`PBF_COMPRESSED`/`pb_zref` phys_block extension, and the
`vsi_z*` stats are all implemented on branch `feature/vm-zram-b1`
(WIP commit — do not merge).

### What works
- **Codec + core roundtrip: byte-exact correct.** A single process
  whose *idle* pages are compressed out by another process's memory
  pressure reads them back identically (verified indirectly).
- **Compression ratio ~8x.** Under a 400 MB `MAP_CONTIG` pressure
  spike at -m192, 12,586 anonymous pages were compressed out
  (~49 MB) into 1,591 pool pages (~6.2 MB) while the system stayed
  fully alive (execs continued to work).

### Open blockers (must be fixed before shipping)
1. **Compress-out does not relieve a process's *own* growth.** When a
   single process's active working set exceeds RAM, its pages are all
   hot; compress-out (invoked from that process's own failing fault)
   frees nothing useful and the process OOM-SIGSEGVs — the pre-existing
   no-OOM-killer behavior, not a regression, but it means the naive
   "one process fills past RAM" test cannot benefit. The genuine win
   (compressing an *idle* process's cold pages to make room for an
   *active* one) is real but under-exercised by the current harness.
2. **Tight-RAM hang.** At -m128 with ~80 MB single-process fill the
   guest wedges (no serial output, no flush) instead of cleanly
   OOMing. Root cause not yet isolated (alloc-retry vs compress-sweep
   trace did not capture before the wedge). Needs a guaranteed
   forward-progress guarantee: a `cache_freepages()` that reports
   pages actually *net*-freed, and an alloc-retry loop that gives up
   (clean ENOMEM) rather than spinning when net progress stalls.
3. **`proc_is_runnable` panic under SMP** (`kernel/proc.c` ~:517).
   Compress-out's `pt_writemap(MAP_NONE)` brackets the target with
   `VMCTL_VMINHIBIT_SET`/`CLEAR` (pagetable.c). On another CPU this can
   flip an actively-dispatched process non-runnable *inside*
   `switch_to_user`'s message-delivery loop, tripping the inner
   `assert(proc_is_runnable(p))`. This is a **pre-existing kernel
   race** (A1 has the same exposure; B1's fork/message-heavy workload
   just triggers it constantly). The correct fix is in the kernel:
   make that inner assert a re-pick (`if(!proc_is_runnable(p)) goto
   not_runnable_pick_new;`), mirroring the recheck already present at
   the top of the same loop.

### Debugging notes for whoever resumes
- **VM `printf` DOES reach the serial console** (with `consdev=com0`),
  but is garbled by concurrent SMP console writers (`VM:`->`VSM:`,
  `kernel`->`kSernel`); `grep` after stripping `[><]` works.
- A **fork-free `/proc/meminfo` streamer** (`/tmp/mimon.c`) is the
  robust instrument: it keeps reporting through a fork/exec wedge and
  only stops on a total VM lockup; its last line fingerprints the
  wedge. meminfo now has 18 fields (…, evicted, zblobs, zpool, zin,
  zout).
- **Do not** send qemu-monitor `sendkey` mid-run to a serial console
  guest — it corrupts the console stream and fakes a wedge.
- The right B1 validation is asymmetric: process A holds patterned
  *idle* anon pages; process B applies pressure (MAP_CONTIG) to
  compress A's pages out; A then verifies byte-for-byte. Confirm
  `zin>0` and `zout>0` bracket the verify.

## Phase B1 update (kernel race FIXED; decompress+COW bug found)

- **Kernel `proc_is_runnable` SMP race: FIXED and validated.** See the
  `kernel/proc.c` commit on this branch. The compress-out path's
  `VMINHIBIT` (via `pt_writemap(MAP_NONE)`) could flip an
  actively-dispatched process non-runnable inside `switch_to_user`'s
  misc-flags loop, tripping a bare `assert(proc_is_runnable(p))`. Made
  it a re-pick, matching the two rechecks already bracketing that loop.
  The asymmetric zram workload that reliably panicked at -smp 2 now
  runs with no panic; compression (zin) and decompression (zout) both
  engage without crashing VM. This fix is independent of B1 (A1 has the
  same exposure) and is worth landing on its own.

- **New open bug: decompress + COW of a *shared* compressed page
  SIGSEGVs the reader.** Reproducible at -smp 2 with ~99 MB free (so
  NOT OOM): during an asymmetric run, ~51 decompress-ins succeed
  (zout climbs 5 -> 51) and then a process SIGSEGVs, deterministically.
  The prime suspect is the decompress path in `anon_pagefault` for a
  post-fork shared page (`refcount > 1 && write`), which after
  decompressing into `new_page` calls
  `mem_cow(region, ph, MAP_NONE, MAP_NONE)`. The two `MAP_NONE`
  arguments are almost certainly wrong for `mem_cow`'s contract — it
  likely needs the region's allocated COW page, not a sentinel — so the
  faulting process ends up with a bad mapping. Fixing this needs a
  careful read of `mem_cow`'s signature and the materialize-then-COW
  ordering (materializing `new_page` onto the *shared* pb before
  COWing a private copy for the faulter may itself be the wrong shape;
  consider COWing first into a fresh page and decompressing into that).

- Net B1 state: compression correct and effective; refcount==1
  decompress correct; **shared-page decompress+COW is buggy**;
  extreme single-proc overcommit still OOMs (no OOM killer);
  a tight-RAM livelock remains un-root-caused (VM-printf tracing was
  found unreliable, so a stats-only or panic-dump instrument is needed).

## Phase B1 RESOLVED and validated

The phase-B1 anonymous-memory compressor is correct and works at
-smp 2 and -smp 4.  The failures described in the two sections above
turned out to be **two test-harness artifacts plus one real
performance bug**, not defects in the compress/decompress/COW logic:

1. **The "SIGSEGV" was a broken test binary.**  An instrumented `zrw`
   grew a `SIGSEGV`/`SIGBUS` `sigaction()` handler; that handler itself
   faulted, so the process died with an *uncatchable* segfault that had
   nothing to do with VM.  A `zrw` without the handler passes byte-for-
   byte in every scenario (fill-fits, single-process self-overcommit,
   and fork+COW), including under memory pressure.

2. **The "hangs" were harness timeouts.**  Heavy compression is slow;
   a 110 MB fill+verify at -m160 needs well over a minute, and the
   verify-poll windows were too short, so a slow-but-correct run was
   mis-reported as a hang.  With a generous timeout the same run
   returns `ZRW_OK` and the system is responsive afterwards.

3. **One real bug was fixed: per-fault TLB-flush churn.**  The original
   compress-out / decompress-in paths mapped each target frame into VM's
   own address space with `vm_mappages()`/`vm_unmappage()`, each of
   which issues a `VMCTL_FLUSHTLB` -- a TLB flush from deep inside the
   page-fault path, on every single page.  This is now replaced by a
   permanently-mapped scratch page plus `sys_abscopy()` (physical ->
   physical), exactly as `mem_cow()` does:
   - `zstore_put_phys(src_phys)` abscopies the frame into the scratch
     page, detects an all-zero page (returns `ZSTORE_ZERO`), otherwise
     LZ4-compresses from the scratch page.
   - `zstore_get_phys(handle, dst_phys)` decompresses into the scratch
     page and abscopies it to the destination frame.
   No per-fault VM mapping or TLB flush remains in either hot path.

### Validation (clean `zrw`/`zfork`, generous timeouts)
- **fill-fits** (20 MB @ -m160): `ZRW_OK`, no compression, baseline.
- **self-overcommit** (110-120 MB @ -m160): `ZRW_OK` byte-exact;
  heavy compress-out during fill and decompress-in during verify.
- **fork + COW** (`zfork`, -smp 2 and -smp 4): parent fills, a fork
  makes the compressed pages shared (refcount > 1); the child reads
  them back (shared decompress) and the parent overwrites every page
  (refcount > 1 + write => decompress-then-`mem_cow`).  Both verify
  byte-exact, with and without a concurrent 400 MB `MAP_CONTIG`
  pressure spike, and the system stays responsive.

### Remaining notes / future work
- Extreme *contiguous* pressure (a single 400 MB `MAP_CONTIG` request)
  makes compress-out compress essentially all anonymous memory before
  the request still fails on fragmentation; it completes but is slow.
  This is an artificial trigger; ordinary anonymous growth (the zram
  use case) is unaffected.
- A single process whose *own* live working set exceeds RAM still
  OOM-SIGSEGVs (no OOM killer); compression relieves *cold* pages of
  *other* processes, which is the intended win.
- B2 can add coldness tracking (accessed-bit scan) and proactive
  compression below a deeper watermark.

## Phase B2: coldness selection + proactive compression

Two refinements to the B1 compressor, both validated byte-exact at
-smp 2 and -smp 4 (fork+COW with/without a 400 MB MAP_CONTIG spike, and
110 MB single-process self-overcommit), system responsive throughout.

### B2a - accessed-bit coldness (clock / second chance)
`map_compress_anon_pages(target, cold_only)` no longer takes pages in
plain round-robin order.  It runs a two-pass clock:
- **Pass 0 (cold):** for each eligible anon page, consult the hardware
  accessed bit via `pt_test_and_clear_accessed(vmp, vaddr)`.  A page
  that was accessed since it was last aged is *skipped* and its accessed
  bit is *cleared*, so a genuinely hot page keeps getting a second
  chance while its bit is re-set on the next access; only pages that are
  still cold are compressed.
- **Pass 1 (any):** the hard-failure path must free memory, so if pass 0
  did not reach `target`, a second pass compresses any eligible page
  regardless of the accessed bit.  Each pass gets its own `visited`
  budget so a pass 0 that spent its budget aging hot pages cannot starve
  pass 1 (which would otherwise return 0 and break the allocator's
  reclaim retry loop).

`pt_test_and_clear_accessed()` reads/clears the A bit directly in the
page-table entry (native `pte_t` width - a u32_t temporary would clobber
the high bits of an amd64 PTE).  It is a pure usage hint: clearing it
changes no mapping and needs no VMINHIBIT/TLB handshake; a stale TLB
entry can at worst make a warm page look cold and get compressed, which
is a performance miss, never a correctness problem.

### B2b - proactive compression at the watermark
`cache_freepages()`'s second argument is now a mode: 0 = free unmapped
cache only (A2 proactive), 1 = hard failure (cache + evict clean files +
compress cold-then-any), 2 = proactive compress-cold.  When an
allocation drives the free count below the low watermark and freeing
clean cache pages does not restore headroom, VM now proactively
compresses a bounded batch (`RECLAIM_COMPRESS_MAX` = 512) of *cold*
anonymous pages (mode 2) instead of waiting for a hard allocation
failure.  The `below_low_watermark` latch (re-armed at the high
watermark) keeps the pool-page allocations made during that compression
from re-triggering it, and the batch cap bounds the per-allocation stall.

### Future work (B3+)
- Shrink empty zstore pool pages back to the allocator (the pool
  currently only grows, capped at total/4).
- OOM-policy hardening: a process whose own live working set exceeds RAM
  still SIGSEGVs (no OOM killer).
- Optionally back the zstore with a disk swap device (phase C).
