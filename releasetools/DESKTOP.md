# Building the LXQt/Qt6 desktop into an image

The MINIX desktop (wlcompd + LXQt on Qt 6, software-rendered over Wayland) is an
**out-of-tree** stack: glib, D-Bus, Qt 6, KDE Frameworks bits and the LXQt
components are CMake/meson/autotools packages, not part of the reachover world.
So a normal `build.sh release` builds the compositor (`wlcompd`/`wlclient`, which
*are* reachover programs) but **not** the desktop apps — they have to be built and
installed into `DESTDIR` as a separate, opt-in stage.

This directory automates that:

- `desktop-sources.manifest` — pinned upstream sources (version + URL + sha256).
- `build-desktop.sh` — fetches, patches, cross-builds and installs the whole
  stack into a `DESTDIR`, in dependency order. It is the executable form of the
  recipes in `external/{glib,dbus,qt6,kf6,lxqt,xdg-user-dirs}/README.md`.
- `amd64_cdimage.sh` grows an opt-in `MKDESKTOP=yes` that overlays the result
  onto the ISO.

## Host prerequisites

The cross build needs, on the build host:

- `meson`, `ninja`, `cmake`, `curl`, `patch`, `pkg-config`, `sha256sum`
- **A host Qt of the *same* version as `qtbase` in the manifest (6.11.1).** The
  cross build runs the host `moc`/`rcc`/`uic` via `QT_HOST_PATH`, and they must
  match the target Qt version exactly. Set `QT_HOST_PATH` to that install.

`build-desktop.sh` checks these and fails loudly if any are missing.

## First-time setup: record the checksums

The manifest ships with `sha256 = TOFILL`. The driver refuses to build against a
`TOFILL` entry (so an upstream content swap can't slip in unnoticed). Populate
them once:

    sh releasetools/build-desktop.sh checksums   # fetches each source, prints sha256

Paste the printed column into `desktop-sources.manifest` and commit.

## Building the desktop into DESTDIR

    DESTDIR=<…>/destdir.amd64 \
    TOOLDIR=<…>/tooldir.<host> \
    QT_HOST_PATH=/usr \
    sh releasetools/build-desktop.sh

Components build in order (pcre2 → glib → dbus → Qt 6 → qtsvg → ECM/kwindowsystem/
layer-shell-qt → lxqt-build-tools → libqtxdg → liblxqt → the LXQt apps →
qterminal → xdg-user-dirs). The driver records everything it installed in
`<DESKTOP_WORK>/install.list` (default `<destdir-parent>/desktop-build/`).

## Putting it on an image — two ways

**A. In the sets (installable systems).** Install the desktop into `DESTDIR`
between `distribution` and `sets`, then the existing `minix-base` set list packs
it automatically (the entries `./usr/bin/startlxqt`, `lxqt-session`, … are
already there):

    build.sh -m amd64 -O ../build.amd64 -U distribution
    sh releasetools/build-desktop.sh                       # into DESTDIR
    build.sh -m amd64 -O ../build.amd64 -U sets
    bash releasetools/amd64_cdimage.sh

**B. Overlay onto the ISO (live CD, no re-`sets`).** Fits the usual two-command
flow — build the world/sets normally, then overlay at image time:

    build.sh -m amd64 -j24 -O ../build.amd64 -U release
    sh releasetools/build-desktop.sh                       # into DESTDIR
    MKDESKTOP=yes OBJ=../build.amd64 bash releasetools/amd64_cdimage.sh

`MKDESKTOP=yes` copies the `install.list` files straight from `DESTDIR` into the
ISO tree and emits an `extra.desktop` mtree spec for them. Off by default, so a
plain `amd64_cdimage.sh` is unchanged.

## Running it

Boot **UEFI** (`-cpu host`, OVMF code+vars pflash) so the GOP framebuffer gives
you `/dev/fb0`, log in, and:

    startlxqt        # brings up the fb driver, wlcompd, D-Bus and lxqt-session

See `external/lxqt/README.md` for the session details and `external/qt6/README.md`
for why everything is linked `-static`.

## Status / things to verify on a real build

`build-desktop.sh` transcribes the READMEs faithfully, but a few specifics could
not be pinned from the tree alone and are marked `TODO(verify)` in the script —
verify them against a known-good build:

- `pcre2` has no `external/pcre2` README; its version (10.44) and flags are
  inferred.
- `qtsvg`, `lxqt-globalkeys`, `lxqt-menu-data`, `qtxdg-tools`, `lxqt-panel` and
  `lxqt-session` use the shared consumer flag template; the READMEs don't give
  their exact `-D` sets. Confirm any component-specific options.
- All manifest URLs follow the standard upstream patterns but are unverified
  until `checksums` fetches them successfully.
