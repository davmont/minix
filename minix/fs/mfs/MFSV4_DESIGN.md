# MFS version 4 — design

Status: **Phase 0 (in progress)** — superblock/version substrate.
Target milestone: MINIX **4.0.0** (amd64, SMP, IPC fastpath, pthreads/LWPs,
modern toolchain). MFS V4 is the on-disk filesystem story for that release.

## 1. Motivation

MFS V3 has served well but carries a set of limitations that are baked into its
on-disk format and therefore cannot be lifted without a format change:

| Limitation | Where (V3) | Consequence |
|---|---|---|
| **2 GB max file size** | `s_max_size`/`d2_size`/`i_size` are `i32_t` (`super.c:323`, `type.h:12`) | files capped at `INT32_MAX`, even though the inode can *address* ~4.3 GB |
| **Year-2038 cliff** | `d2_atime/mtime/ctime` are `i32_t` (`type.h:13-15`) | on-disk timestamps overflow in Jan 2038 |
| **≤65535 hard links** | `d2_nlinks` is `u16_t`, flagged `HACK!` (`type.h:9`) | link count saturates |
| **16-bit gid** | `d2_gid` is `u16_t`, flagged `HACK!` (`type.h:11`) | gids > 65535 unrepresentable |
| **No inode flags** | no field exists | `chflags` (immutable/append-only) impossible |
| **No extended attributes** | no storage | `extattr`/xattr unsupported |
| **O(n) directory lookup** | linear scan (`path.c:135`) | large directories are slow |
| **No crash recovery** | clean/dirty flag only (`mount.c:39`) | unclean shutdown ⇒ offline `fsck` |

Rather than bumping the format once per fix, V4 introduces these together and —
crucially — does so behind an **extensible feature-flag mechanism**, so V4 is
intended to be the *last* format bump: journaling, xattr, directory indexing,
etc. arrive later as feature *bits*, never as V5.

## 2. Compatibility strategy

V4 uses a **new superblock magic** (`SUPER_V4 = 0x4d5b`) plus three 32-bit
**feature-flag masks** modelled on ext2/3/4 and UFS2:

* `s_feature_compat`    — informational; an implementation that does not know a
  bit may still mount read/write and ignore it.
* `s_feature_ro_compat` — an unknown bit here ⇒ mount **read-only** (the feature
  affects on-disk structures a writer must maintain, but a reader can ignore).
* `s_feature_incompat`  — an unknown bit here ⇒ **refuse to mount** (the layout
  cannot be safely interpreted without understanding the feature).

A single MFS driver supports **both V3 and V4**:

* **V3 volumes** continue to be read and written exactly as today. V4-only
  in-core superblock fields are forced to zero when a V3 superblock is read, so
  uninitialised on-disk bytes from old `mkfs` are never misinterpreted.
* **V4 volumes** are recognised by magic; their feature masks are checked
  against the driver's *supported* sets to decide mount disposition (RW / RO /
  refuse).

### Backward-compatibility note (intentional)

A *new* magic means **older MINIX releases cannot read a V4 volume** — their MFS
sees an unknown magic and declines cleanly (it already rejects non-V3 magics in
`read_super`). This is the accepted trade-off and the standard meaning of
"retro-compatibility" here: **the new MINIX reads old (V3) disks**, not the
reverse. Sites needing to move data to an older MINIX keep using V3 (`mkfs.mfs`
without `-4`). The alternative — keeping the V3 magic and gating everything on
feature flags (ext-style) — preserves more reverse compatibility but blurs the
4.0.0 milestone; we choose the explicit version bump.

## 3. On-disk format

### 3.1 Superblock (`struct super_block`)

All existing V3 fields keep their current offsets. New on-disk fields are
appended **after** `s_disk_version` (the previous last on-disk field) and before
the in-memory-only region. `LAST_ONDISK_FIELD` in `super.c` moves to the new
final reserved field so the serialised region covers them. The on-disk
superblock lives at offset 1024 within a ≥4 KB block, so there is ample room.

```
  ... existing V3 fields, through:
  char   s_disk_version;        /* format sub-version */
  /* --- V4 extension (meaningful only when s_magic == SUPER_V4) --- */
  u8_t   s_v4_pad8;             /* explicit padding for deterministic layout */
  u16_t  s_v4_pad16;
  u32_t  s_feature_compat;
  u32_t  s_feature_incompat;
  u32_t  s_feature_ro_compat;
  u32_t  s_v4_reserved[5];      /* reserved; must be zero */
```

On read: if `s_magic != SUPER_V4`, the four V4 fields are zeroed in core.
On write: V3 volumes carry zeroes here (harmless; beyond an old driver's read
window). V4 volumes carry their real masks.

### 3.2 Inode (`d4_inode`, 128 bytes) — Phase 1

V4 replaces the 64-byte `d2_inode` with a 128-byte `d4_inode` (selected by the
`INCOMPAT_WIDE_INODE` feature bit — see §4). All fields naturally aligned:

```
  off  field            type     fixes
  0    d4_size          u64_t    large files (>2 GB)
  8    d4_atime         i64_t    Y2038
  16   d4_mtime         i64_t    Y2038
  24   d4_ctime         i64_t    Y2038
  32   d4_crtime        i64_t    birth time (new)
  40   d4_mode          u16_t
  42   d4_pad0          u16_t
  44   d4_nlinks        u32_t    >65535 links
  48   d4_uid           u32_t    32-bit uid
  52   d4_gid           u32_t    32-bit gid
  56   d4_flags         u32_t    chflags (immutable/append/nodump/...)
  60   d4_xattr_zone    u32_t    zone holding xattrs, 0 = none (future)
  64   d4_zone[10]      zone_t   7 direct + single + double (+1 reserved)
  104  d4_reserved[6]   u32_t    reserved; must be zero
  128  (end)
```

Byte-order conversion uses the existing `conv2`/`conv4` helpers plus a new
`conv8` for the 64-bit fields (Phase 1).

## 4. Feature-flag catalogue

Bits are assigned as phases land. Phase 0 defines the masks and the *supported*
sets; the driver mounts a V4 volume RW only if it understands every incompat and
ro_compat bit set.

```
INCOMPAT   (unknown ⇒ refuse mount)
  0x0001  WIDE_INODE   128-byte d4_inode: large files, 64-bit time,
                       wide nlink/gid, inode flags          (Phase 1/2/3)
  0x0002  JOURNAL      metadata journal present             (future)
  0x0004  XATTR_BLOCK  xattrs in a dedicated zone format    (future)

RO_COMPAT  (unknown ⇒ mount read-only)
  0x0001  HASHDIR      hashed/indexed directories           (future)

COMPAT     (unknown ⇒ ignore, mount RW)
  0x0001  DIR_INDEX_HINT  advisory directory index hints    (future)
```

Phase 0 *supported* sets: `INCOMPAT_SUPPORTED = 0`, `RO_COMPAT_SUPPORTED = 0`.
A plain V4 volume (no flags) therefore mounts RW; any incompat bit refuses; any
ro_compat bit forces RO. Each later phase adds its bit to the supported set.

## 5. Phased plan

* **Phase 0 — substrate (this change).** `SUPER_V4` recognition + feature-flag
  machinery in the MFS server; `mkfs.mfs -4` creates a V4 superblock; `fsck.mfs`
  recognises V4. A plain V4 volume behaves exactly like V3 (V3-layout inodes;
  no incompat bits). Validates the version/flag plumbing end-to-end.
* **Phase 1 — wide inode (`INCOMPAT_WIDE_INODE`).** `d4_inode` (128 B), 64-bit
  timestamps (Y2038), 32-bit nlink/gid, inode flags field, `crtime`; widen the
  in-core inode and `read_map`/`write_map` accordingly. `conv8`.
* **Phase 2 — large files.** Lift the `INT32_MAX` cap on V4; `u64` size paths
  and the `int`-cast positions in `read.c`/`write.c`. Validate >2 GB round-trip.
* **Phase 3 — file flags.** `chflags`/`fchflags` via `d4_flags`
  (immutable/append-only first).
* **Phase 4+ — larger feature bits**, each on its own flag: hashed directories
  (`HASHDIR`, ro_compat) → extended attributes (`XATTR_BLOCK`) → metadata
  journaling (`JOURNAL`).

Two improvements are **format-independent** and apply to V3 and V4 alike; they
are tracked separately and may land before/around Phase 1: **noatime/relatime**
mount options and **fdatasync**.

## 6. Boot / root filesystem policy

The boot and root filesystems **stay V3** until V4 is proven and the boot path
(boot monitor / kernel ramdisk / `mkfs` defaults) can read V4. V4 is first
exercised as a **data filesystem** (a second partition / image). The default
`mkfs.mfs` output remains V3; `-4` opts in. The default flips to V4 only once
the full feature set and the boot path are validated.

## 7. Validation strategy

Per phase, in QEMU on amd64:

* `mkfs.mfs -4` a V4 image; mount RW; create/read/write/append/truncate/unlink
  files and directories; unmount; `fsck.mfs` reports clean.
* Confirm a **V3** image still mounts and round-trips unchanged.
* Confirm feature-flag semantics by injecting bits: an unknown **incompat** bit
  ⇒ mount refused; an unknown **ro_compat** bit ⇒ mount forced read-only.
* Host cross-check where possible (structural dump of the V4 superblock/inode).

## 8. Open questions

* Whether `LARGE_FILE` should be ro_compat (readers tolerate, writers maintain)
  or simply implied by `WIDE_INODE` (incompat). Current plan folds it into the
  wide inode (incompat) for simplicity.
* xattr storage layout (inline vs dedicated zone) — deferred to its phase.
* Journaling scope (metadata-only, ordered) — deferred; the `JOURNAL` incompat
  bit reserves the design space.
