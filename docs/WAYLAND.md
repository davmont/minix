# Running Wayland on MINIX — feasibility and roadmap

Now that MINIX has a working userland graphics stack (the `/dev/fb0` fb
driver, pixman + FreeType, and the `fbcompd` compositor — see
`docs/examples/compositor/`), this note assesses what it would take to run
**Wayland**, and records what was verified on the tree and on a running
system.

## TL;DR

Feasible, but **one hard kernel/VM prerequisite must land first**: an
fd-backed, writable, cross-process shared-memory object (POSIX `shm_open`
or a `memfd`). Without it, `wl_shm` — how every Wayland client hands pixels
to the compositor — cannot work. Everything else Wayland strictly requires
is already present; the rest (libffi, xkbcommon, meson) is ordinary porting.

## The two make-or-break primitives — verified on-target

A small probe (`shm_open`-style test, run under QEMU+OVMF) checked the two
things Wayland cannot live without:

| primitive | result | evidence |
|---|---|---|
| **fd-passing** over AF_UNIX (`SCM_RIGHTS`) | **WORKS** | probe: `FD-PASS: SCM_RIGHTS -> OK`; libc `sendmsg`/`recvmsg` implement ancillary data via the UDS server (`minix/lib/libc/sys/sendmsg.c`, `recvmsg.c`); covered by `minix/tests/test90.c` |
| **fd-backed writable shared memory** (`wl_shm`) | **MISSING** | probe: `WL-SHM: writable MAP_SHARED(file) -> FAIL (ENXIO)` |

### Why shared memory is the blocker

Wayland's `wl_shm` has a client create a pool from an mmap-able fd
(`memfd_create` or `shm_open`), `mmap(MAP_SHARED, PROT_WRITE)` it, pass the
fd to the compositor over the socket, and both sides map the same pages.
On MINIX today:

- **`shm_open` is not implemented** — the prototype exists in
  `sys/sys/mman.h` but no library defines the symbol and there is no
  backing source. `memfd_create` is absent too.
- **Writable `MAP_SHARED` of a regular file is refused.** The VM rejects it
  explicitly:
  ```c
  /* minix/servers/vm/mmap.c */
  /* For files, we only can't accept writable MAP_SHARED mappings. */
  if ((m->m_mmap.flags & MAP_SHARED) && (m->m_mmap.prot & PROT_WRITE))
          return ENXIO;
  ```
- **SysV shm works** (`shmget`/`shmat`; this is what `fbcompd` uses) but is
  keyed by an integer id, not an fd, so it cannot be passed via `SCM_RIGHTS`
  and is not what the `wl_shm` protocol expects.

The good news: the VM already supports **writable `MAP_SHARED` for
anonymous regions** (`mmap.c`: `VR_WRITABLE | VR_ANON`). So the building
block — shared, writable pages — exists. What is missing is a *named,
fd-backed* handle to such a region that an unrelated process can be handed.
Implementing `shm_open`/`memfd` on top of the existing anon-shared machinery
is therefore the concrete first prerequisite, not a from-scratch effort.

## What else Wayland needs — inventory

Already present (no work):

- AF_UNIX sockets + `SCM_RIGHTS` ancillary data (see above).
- `kqueue` (`sys/sys/event.h`) — libwayland's event loop uses epoll on
  Linux but also runs on poll/kqueue.
- An input source: the MINIX input server (`/dev/kbdmux`, `/dev/mousemux`,
  raw USB-HID `struct input_event`). Wayland normally uses libinput+evdev,
  which do **not** apply here; a compositor reads these devices directly, as
  `fbcompd` already does. Not a blocker — just a custom input backend.
- A software rendering path: pixman + FreeType + `libfbgui` → `/dev/fb0`.

Missing but portable (not blockers):

- **libffi** — libwayland's generated marshalling needs it. Not in tree;
  portable from NetBSD pkgsrc.
- **xkbcommon** — keymap handling. Portable, or hardcode a US keymap for a
  PoC (as `fbcompd` does).
- **meson/ninja** — Wayland/Weston's build system. Bootstrap from pkgsrc, or
  write reachover BSD Makefiles for libwayland (it is a small library).

Not available, worked around by design:

- **No GPU / Mesa / DRM-KMS** → software rendering only (pixman). Fine for a
  compositor; rules out Weston's GL renderer and its drm backend.
- **No epoll, no libinput/evdev, no Weston fbdev backend** → use kqueue/poll,
  the MINIX input server, and a custom `/dev/fb0` backend respectively.

## Recommended path

**A lightweight custom Wayland compositor on `libwayland-server`**, reusing
the `fbcompd` architecture (pixman/FreeType/`libfbgui` for output, the MINIX
input server for input), rather than porting Weston (which additionally
needs a resurrected fbdev backend, xkbcommon, and full meson). The
compositor teaches an `fbcompd`-derived server the Wayland wire protocol for
`wl_compositor` / `wl_shm` / `wl_surface` / `xdg_shell` / `wl_seat`.

## shm_open design — how deep the blocker really is

A closer look shows `shm_open` is **not a libc addition; it is a core
VM/VFS feature**, because the file-mmap path cannot share writable pages:

- `minix/servers/vm/mem_file.c`: `mappedfile_writable()` returns *"never
  writable"*, and a write to a file-backed page triggers **copy-on-write
  into private anonymous memory** (`mappedfile_pagefault`). So even if the
  `MAP_SHARED && PROT_WRITE` rejection in `mmap.c` were removed, two
  processes mapping the same file would get **private** copies — no sharing.
- The **only** mechanism that shares writable pages across unrelated
  processes is `vm_remap` / `mem_shared` — precisely what SysV `shmat` uses:
  the `ipc` server holds a `MAP_ANON` region and `vm_remap`s those physical
  pages into each attacher (`minix/servers/ipc/shm.c`,
  `minix/servers/vm/mmap.c:do_remap`).
- There is no chardriver mmap shortcut (`/dev/fb0` is written with
  `write()`/`lseek()`, not mmapped), and `libvtreefs` exposes no mmap/peek
  hook.

So a working `shm_open` needs three cooperating pieces:

1. **A holder + fd source.** A small service holds one `MAP_ANON` region per
   shm object (as `ipc/shm.c` already does) and mints a real fd for it —
   either a new `shmfs`, or reusing the anonymous-inode path VFS already has
   (`req_newnode(PFS_PROC_NR, ...)`, used by `pipe()`/`socketpair()`).
2. **mmap routing.** `mmap(shm_fd)` must reach `vm_remap` of the holder's
   region, not the COW `mappedfile` path. Cleanest: VM's `do_mmap`, when the
   `FDLOOKUP` resolves to the shm object's device, asks the holder to remap
   its region into the caller (a localized VM/VFS change) rather than a
   generic libc mmap shim.
3. **libc + plumbing** — `shm_open`/`shm_unlink`, message protocol, service
   config, `/dev/shm`, set lists.

This is genuinely core-kernel work (a service + VM/VFS routing), tested by
rebuilding the boot image each iteration. It is tractable and reuses the
proven `vm_remap`, but it is much larger and higher-risk than "add a libc
function".

## Prerequisite roadmap (ordered)

1. **Shared memory (the blocker).** Implement `shm_open` per the design
   above: a holder service + fd source + `mmap`→`vm_remap` routing, reusing
   the SysV-shm `vm_remap` mechanism. Validate with the fd-pass +
   cross-process shared-write probe (`wlprobe`).
2. **libffi** — port/build for MINIX.
3. **libwayland** — build `libwayland-server`/`-client` (poll or kqueue
   event loop); reachover Makefile or bootstrapped meson.
4. **Keymap** — xkbcommon, or a minimal keymap to start.
5. **Compositor** — extend an `fbcompd`-style server to speak Wayland:
   `wl_shm` over the new shm fds, `wl_surface` commit → pixman composite →
   `/dev/fb0`, `wl_seat`/`wl_keyboard`/`wl_pointer` from `/dev/kbdmux` and
   `/dev/mousemux`.

**First concrete step:** implement and verify `shm_open`. Until that lands,
Wayland is blocked; after it, the remaining work is well-trodden porting.
