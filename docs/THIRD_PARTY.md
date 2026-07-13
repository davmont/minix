# Third-party software: inventory, upgrade process, display-stack decision

Status as of July 2026 (devel).  This records the results of the 2026-07
external-package audit and the conventions for keeping bundled software
current.

## Layout

There is no ports tree.  Upstream code is vendored NetBSD-style: pristine
(or NetBSD-patched) sources in `<pkg>/dist/`, BSD reachover Makefiles next
to them listing `SRCS`, and curated config headers outside `dist/`.

| Location | Contents |
|---|---|
| `external/{apache2,bsd,gpl2,gpl3,historical,lgpl3,mit,mpl,public-domain}/` | userland packages by license |
| `crypto/external/apache2/` | OpenSSL 3.5 LTS |
| `crypto/external/bsd/` | Heimdal, netpgp, libsaslc |
| `external/lgpl2/userspace-rcu/` | liburcu (BIND 9.20 dependency) |
| `minix/lib/liblwip/dist/` | lwIP (the MINIX network stack) |
| `sys/external/bsd/compiler_rt/` | LLVM runtime |

## Upgrade process (per package, one commit each)

1. Fetch the upstream release tarball; verify its published SHA-256
   (GitHub releases API exposes asset digests).
2. Determine whether `dist/` is pristine or NetBSD/MINIX-patched:
   `grep -rl '$NetBSD\|__minix' dist/` and, when in doubt, diff `dist/`
   against the pristine tarball of the *currently bundled* version.
3. Pristine dist: replace wholesale.  Patched dist: three-way merge
   (`git merge-file`) with base = pristine old version, ours = current
   dist, theirs = new upstream.  Never silently drop a `__minix` guard;
   check first whether upstream made it obsolete.
4. Reconcile reachover `SRCS` lists and curated `*config*.h` against the
   new source layout; regenerate generated files (perlasm `.S`, `.pc`).
   OpenSSL x86_64 perlasm regeneration needs `LC_ALL=C` (localized
   assembler banners break the GNU-as probe in `x86_64-xlate.pl`).
5. Bump `shlib_version` minor when upstream adds public APIs.
   Exact shlib minors in `distrib/sets/lists/base/shl.mi` are not
   enforced by this fork's release checks; major bumps do need set-list
   updates.
6. Build test: per-package
   `EXTERNAL_TOOLCHAIN=$OBJ/ext-tc $TOOLDIR/bin/nbmake-amd64 dependall`
   (install the lib subdir into DESTDIR first if a bin links against a
   stale installed archive), then a full `build.sh` release per batch.

## Version status (2026-07 audit)

Current after the 2026-07 upgrade batch: LLVM/clang 22.1.7, OpenSSL
3.5.7 LTS, expat 2.8.2, zlib 1.3.1, sqlite 3.53.3, less 668, tmux 3.6a,
libevent 2.1.13, xz 5.8.3, libarchive 3.8.8, lua 5.4.8, file 5.46,
tz 2026a, libpcap 1.10.6, tcpdump 4.99.6 (both resynced from
NetBSD-current; ndp vendors gmt2local.c, netstat's bpf_dump renamed
nsbpf_dump), BIND 9.20.24 + liburcu 0.15.0, dhcpcd 10.3.1, bzip2 1.0.8,
lwIP 2.2.1.  ISC DHCP (server/relay) retired.
The batch was validated with a full amd64 release build and a QEMU
boot of the ISO to login, running the upgraded binaries in-system.

Landed since the audit, with notes:

- **OpenSSL 3.5.7 LTS landed 2026-07** (resync from NetBSD-current's
  crypto/external/apache2/openssl; the 3.0 tree is gone).  All nine
  consumers build unchanged.  MINIX specifics: POSIX thread backend
  with a __minix guard making thread-spawn report unavailable (libc
  pthread stubs carry the provider/RCU machinery in static binaries;
  see the "fix 3.5 crypto on MINIX" commit for the failure mode),
  RSAZ/AVX-IFMA asm excluded, QUIC safe-math macro rename for the
  NetBSD uint64_t macro collision.  Verified in-system: provider
  loads, RAND/EVP work.
- **BIND 9.20.24 landed 2026-07** (resync from NetBSD-current, with the
  new liburcu 0.15.0 dependency imported at external/lgpl2 and ported
  to MINIX).  Build and packaging are fully validated; the DNS client
  tools (dig/host/delv/nsupdate) work in-system.  **Runtime status
  (revised after investigation):** the initially observed named(8)
  crashes had two environmental causes, both resolved — the install-CD
  ramdisk (128 KB /tmp) contaminated early tests, and the OpenSSL 3.5
  THREADS_NONE bug (see the openssl fix commit) caused the
  "PRNG not seeded" aborts of both dig and named.  With those fixed,
  multi-worker `named -n 2` starts and stays up on the HD image.
  Dedicated pthread/TLS/urcu-mb torture tests all pass on MINIX.
  **Root cause found (2026-07-09):** the "stall" is a multi-worker
  issue, not a general lwIP failure.  With **one worker** (`named -g
  -n 1`) named is flawless — an authoritative zone answers 10/10
  rapid queries correctly, NXDOMAIN and REFUSED behave right, and a
  minimal UDP request/reply over lo0 also passes, so lwIP loopback UDP
  is fine.  With **two or more workers** queries stall.  BIND 9.20's
  netmgr binds one UDP socket per worker on the same address and
  relies on `SO_REUSEPORT` to spread packets across them
  (lib/isc/netmgr/socket.c); MINIX advertises `SO_REUSEPORT` in its
  headers but lwIP does not implement it, and BIND's load-balancing
  paths are Linux/FreeBSD-only, so the per-worker listeners do not
  receive traffic correctly.  Earlier "intermittent hang" reports were
  compounded by querying `. NS` with recursion off (a correct REFUSED,
  not a hang) and by serial-console repaints garbling scripted output.
  **Resolved 2026-07:** `SO_REUSEPORT` is now implemented (see the lwIP
  entry below), so the `-n 1` workaround is no longer needed.  Note the
  failure was blunter than "stall": the second worker's `bind(2)` failed
  with `EADDRINUSE`, since without `SO_REUSEPORT` lwIP would only share
  a port under `SO_REUSEADDR`.
  named is not part of the default boot (`named=NO`).

- **lwIP 2.0.2 → 2.2.1 landed 2026-07**, together with an implementation
  of `SO_REUSEPORT`.  lwIP is the network stack and parses packets off
  the wire; what we shipped predated the fix for CVE-2020-22283 /
  CVE-2020-22284 (ICMPv6 / 6LoWPAN overflows, fixed in 2.1.3) and we
  build with IPv6, including reassembly and forwarding, enabled.
  What the tree actually carried was not release 2.0.2 but a git-master
  snapshot just after it (upstream `7ffe5bfb`); against that true base
  the local delta was only the patches in `minix/lib/liblwip/patches/`,
  so the upgrade was a re-vendor with those rebased.  Two traps worth
  knowing for the next upgrade: upstream now clones a multi-PCB UDP
  datagram with `pbuf_clone(..., PBUF_POOL, ...)`, which can never
  succeed here (we set `PBUF_POOL_SIZE` to 0) and would have silently
  dropped such datagrams -- patch 4 exists for exactly this; and the
  `pbuf_layer` enum values are now header offsets rather than ordinals,
  so `PBUF_RAW_TX` equals `PBUF_RAW` without an encapsulation header.

Known-stale, deliberately deferred (each needs its own effort):

- Bootstrap-entangled and left alone: gmake 3.81, texinfo 4.8,
  binutils 2.34, flex, nvi, elftoolchain, heimdal 7.8.0 (which is
  upstream's newest release anyway), netpgp.
- netpgp is unmaintained upstream and is a *removal* candidate rather
  than an upgrade one, like ISC DHCP before it: its maintained successor
  is RNP (BSD-3, descends from netpgp, used by Thunderbird), but RNP is
  C++17/CMake with a Botan-or-OpenSSL backend, which is a poor fit for
  base -- pkgsrc already packages it.

Removed 2026-07 with owner sign-off: GCC 4.8.5 and its gmp/mpfr/mpc
dependencies (~312 MB; the trees were never even tracked by git —
untracked files on disk plus MKGCC-gated build hooks).  The toolchain
is clang/LLVM-only; `MKGCC=yes` is no longer supported.

## Display stack decision (2026-07)

X11 was removed from the tree (commit `external/mit/xorg: remove
unbuildable X11 reachover scaffolding`): what the tree carried was
reachover glue for a NetBSD `xsrc` snapshot (~xorg-server 1.10) that was
never part of this repository; `MKX11` defaulted to off and could not
build.  X clients, if ever wanted, come from pkgsrc.

Wayland was evaluated and rejected for now.  MINIX lacks the substrate
Wayland compositors assume: no DRM/KMS (Linux kernel infrastructure),
no evdev/libinput input layer, no epoll/kqueue for libwayland's event
loop, and `wl_shm` needs stronger shared-`mmap` semantics than MINIX
guarantees.  The protocol itself (unix sockets + SCM_RIGHTS fd passing)
is within reach if those gaps close.

Chosen direction: modernize the framebuffer path, which already works
(UEFI GOP linear-framebuffer console on amd64, rendered by the userspace
TTY driver from `kinfo.fb_*`; VGA text on legacy BIOS; OMAP fb driver on
ARM).  Roadmap:

1. **DONE 2026-07**: x86_64 backend for `minix/drivers/video/fb`
   exposing the GOP framebuffer as `/dev/fb0` (userspace driver, no new
   kernel surface); validated under OVMF with a gradient-write test and
   screendump pixel verification.  See the fb backend commit for the
   coexistence model with the TTY console and noted follow-ups.
2. **DONE 2026-07 (validated, no code needed)**: the full input stack
   already exists on amd64 — pckbd handles the PS/2 aux port (IRQ 12,
   packet assembly) and the input server multiplexes to
   `/dev/mouse0-3`/`/dev/mousemux`; verified with QEMU-injected
   motion/button events and a keyboard regression check.  Note for a
   future GUI: `/dev/mousemux` is root-only 0600.
3. Minimal graphics userland on `/dev/fb0` — graphics without a display
   server.  **In progress 2026-07:**
   - **DONE**: pixman 0.46.4 vendored (external/mit/pixman) — 2D pixel
     compositing; validated rendering to the GOP framebuffer via
     /dev/fb0 (screendump: real alpha compositing).
   - **DONE**: FreeType 2.14.3 vendored (external/mit/freetype) — text;
     validated anti-aliased glyph rendering composited through pixman
     onto /dev/fb0.
   - With pixman + freetype + /dev/fb0 + /dev/mouse, the amd64 graphics
     stack has every core piece for a windowed GUI; an interactive
     desktop PoC (composite + drag with mouse input) demonstrates it.
   - Follow-ups: pixman SSE2 fast paths; freetype system-zlib/HarfBuzz;
     a font package (base ships no TTF); a reusable fb toolkit lib.
4. Optional research milestone once shm-mmap and a poll-based event shim
   exist: minimal libwayland + custom fb-backend compositor.
