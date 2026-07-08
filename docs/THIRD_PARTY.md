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
| `crypto/external/bsd/` | OpenSSL, Heimdal, netpgp, libsaslc |
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
3.0.21, expat 2.8.2, zlib 1.3.1, sqlite 3.53.3, less 668, tmux 3.6a,
libevent 2.1.13, xz 5.8.3, libarchive 3.8.8, lua 5.4.8, file 5.46,
tz 2026a, libpcap 1.10.6, tcpdump 4.99.6 (both resynced from
NetBSD-current; ndp vendors gmt2local.c, netstat's bpf_dump renamed
nsbpf_dump), BIND 9.18.24 (external/mpl), dhcpcd 9.4.1, bzip2 1.0.8.
The batch was validated with a full amd64 release build and a QEMU
boot of the ISO to login, running the upgraded binaries in-system.

Known-stale, deliberately deferred (each needs its own effort):

- **OpenSSL 3.0 branch leaves security support 2026-09-07** — plan the
  3.5 LTS migration (API-compatible, but providers/deprecations need a
  sweep of heimdal/netpgp/libsaslc consumers).
- **BIND 9.18 is EOL (June 2026)** — 9.20.x requires libuv >= 1.48;
  the libuv 1.52.1 upgrade unblocks this.
- **ISC DHCP 4.3.0 is EOL upstream (no fixed release exists)** — retire
  server/relay, keep dhcpcd as the client story (10.x upgrade separate).
- **lwIP 2.0.2** → 2.2.x: OS-stack surgery, MINIX glue in
  minix/net/lwip.
- Bootstrap-entangled and left alone: gmake 3.81, texinfo 4.8,
  binutils 2.34, flex, nvi, elftoolchain, heimdal 7.8.0, netpgp.

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

1. x86_64 backend for `minix/drivers/video/fb` exposing the GOP
   framebuffer as `/dev/fb0` (userspace driver, no new kernel surface).
2. Input: surface PS/2 aux bytes already reaching `pckbd` as
   `/dev/mouse`; raw keyboard event mode beside the TTY path.
3. Minimal graphics userland on `/dev/fb0`: pixman as pixel library plus
   a micro-GUI (Microwindows/Nano-X-class) — graphics without a display
   server.
4. Optional research milestone once shm-mmap and a poll-based event shim
   exist: minimal libwayland + custom fb-backend compositor.
