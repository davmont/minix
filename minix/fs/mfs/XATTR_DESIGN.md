# MFS extended attributes — design

Status: **implemented.**
Feature bit: `MFS_INCOMPAT_XATTR_BLOCK` (0x0004), a V4 incompat feature.

## 1. Goal

Support per-file extended attributes through the native BSD `extattr` interface
(`extattr_{get,set,list,delete}_{fd,file,link}`), with the `user` and `system`
namespaces.  The Linux `*xattr` family can later be layered on top as a thin
libc shim with no change to the on-disk format or the file server, because the
VFS↔FS protocol already carries a `(namespace, name, value, flags)` tuple.

## 2. On-disk format

Extended attributes require the V4 wide inode (`MFS_INCOMPAT_WIDE_INODE`), whose
`d4_xattr_zone` word points at a single zone holding all of that inode's
attributes (0 = none).  The zone begins with a header and is followed by packed,
4-byte-aligned entries (see `xattr.h`):

```
struct mfs_xattr_hdr { u32 xh_magic="MXA1"; u32 xh_count; }
repeated xh_count times:
  struct mfs_xattr_ent { u8 ns; u8 namelen; u16 vallen; }
  char name[namelen];        /* no NUL stored */
  char value[vallen];
  pad to a 4-byte boundary
```

All of an inode's attributes must fit in this one zone; a set that would
overflow it fails with `ENOSPC`.  The attribute block is metadata: it is marked
dirty with `MARKDIRTY` and so is journalled when a journal is present.

The `MFS_INCOMPAT_XATTR_BLOCK` feature bit is set **lazily** — the file server
sets it (and rewrites the superblock) the first time an attribute is stored on a
filesystem that lacks it.  A freshly made V4 filesystem therefore stays mountable
by an older (pre-xattr) driver until it actually contains attributes; once it
does, the incompat bit makes such a driver refuse it, since that driver would
zero `d4_xattr_zone` on any inode write and leak the block.

## 3. Operations (`xattr.c`)

* **get** — find `(ns, name)`; copy the value out, or with a zero-length buffer
  return the value's size, or `ERANGE` if the buffer is too small, or `ENOATTR`.
* **set** — allocate the zone on first use; replace any existing entry by
  removing it and appending the new one at the end; `ENOSPC` if it will not fit.
* **list** — emit the names in one namespace in the length-prefixed
  `EXTATTR_LIST_LENPREFIX` format (one byte of length, then the name).
* **delete** — remove the entry; free the zone when the last attribute goes.

The zone is also released when the inode itself is freed (`put_inode`), and
`fsck.mfs` accounts for each inode's `d4_xattr_zone` so the block is never
mistaken for a free or unreferenced zone.

## 4. Pipeline

syscall (`libc/sys/m_extattr.c`) → VFS (`servers/vfs/xattr.c`: `do_extattr`,
which resolves the path/fd, enforces the namespace's access policy, and grants
the value buffer) → `libfsdriver` (`fdr_{get,set,list,remove}xattr`) → MFS
(`xattr.c`).  The `system` namespace is restricted to the super-user; the `user`
namespace follows the file's read/write permission bits.
