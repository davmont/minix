# LXQt libraries on MINIX

`libqtxdg` 4.4.0, `liblxqt` 2.4.0, `lxqt-globalkeys` 2.4.0, `lxqt-menu-data` 2.4.0,
`qtxdg-tools` 4.4.0, `lxqt-panel` 2.4.1, **`lxqt-session` 2.4.0** and
**`qterminal` 2.4.0** (with `qtermwidget` 2.4.0), cross-compiled for MINIX/amd64.

The panel runs and docks itself on wlcompd: the compositor sees a layer surface
`"dock"` on the top layer, gives it the full screen width (1280x32), and receives
its pixels.  It reaches the screen through **LayerShellQt** (see `external/kf6/`),
which speaks wlr-layer-shell -- the protocol wlcompd implements.

Verified on-target by `minix/commands/lxqtprobe` (ALL PASS, headless and on
wlcompd): XDG base directories, desktop-entry parsing, the MIME database,
`XdgMimeApps` through its GLib/GIO backend, and `LXQt::Settings` writing real
config under `/root/.config/lxqt`.

## Order of the build

    lxqt-build-tools 2.4.0   (CMake modules both libraries require)
    pcre2, glib              (see external/glib)
    qtsvg                    (Qt6Svg -- libqtxdg needs it; separate Qt module)
    libqtxdg 4.4.0
    liblxqt 2.4.0

`lxqt-build-tools` is **built natively, not cross-compiled**, and installed into
the sysroot: it ships only CMake modules, and it wants `Qt6CoreTools` (host
moc/rcc), which a cross-built Qt does not provide.  Pass
`-DLXQT_ETC_XDG_DIR=/etc/xdg` so it does not try to run a *target* `qtpaths` on
the build host.

## Configuring libqtxdg / liblxqt

    cmake ../libqtxdg-4.4.0 -GNinja \
      -DCMAKE_TOOLCHAIN_FILE=<destdir>/usr/lib/cmake/Qt6/qt.toolchain.cmake \
      -DQT_HOST_PATH=/usr -DMINIX_NO_STAGING=1 -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_PROJECT_INCLUDE=<...>/external/qt6/cmake/MinixWaylandShim.cmake \
      -DMINIX_EXTRA_STANDARD_LIBRARIES="-lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lintl -lpcre2-8 -lz" \
      -DQTXDGX_ICONENGINEPLUGIN_INSTALL_PATH=/usr/lib/qt6/plugins/iconengines \
      -DLXQT_ETC_XDG_DIR=/etc/xdg -DBUILD_TESTS=OFF -DBUILD_DEV_UTILS=OFF
    ninja && DESTDIR=<destdir> ninja install

liblxqt is the same, plus `-DBUILD_BACKLIGHT_LINUX_BACKEND=OFF` (that backend is
Linux sysfs and drags in PolkitQt6) and
`-DCMAKE_CXX_FLAGS="-D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE -DLXQT_NO_KWINDOWSYSTEM"`.

Note `MINIX_NO_STAGING=1` + plain prefix + `DESTDIR`: both projects install into
`/etc` as well as `/usr`, and an absolute sysconfdir escapes a staging prefix and
lands on the build host.

**Always repeat the `-D__minix` defines when you pass `CMAKE_CXX_FLAGS`.**  Doing
so overrides the toolchain's `CMAKE_CXX_FLAGS_INIT`, and without them
`<sys/errno.h>` never defines `_SIGN`, so every errno constant fails to parse.

## What the patches do, and why

**Everything must be STATIC.**  Both projects hardcode `add_library(... SHARED)`.
Qt has to be linked statically on MINIX -- a dynamic Qt segfaults before `main()`
in `ld.elf_so`'s dynamic TLS -- and a `libQt6Xdg.so` would link static Qt *into
itself* while the application links static Qt too, giving one process two copies
of Qt's global state.  The non-PIC archives will not go into a shared object
anyway.  So: `SHARED`/`MODULE` -> `STATIC`.

**libqtxdg's icon loader collides with Qt's.**  `xdgiconloader` is a fork of Qt's
private `qiconloader.cpp`: it re-implements the member functions of `PixmapEntry`
and `ScalableEntry` -- classes *declared by Qt's own* `private/qiconloader_p.h` --
and defines its own `QIconCacheGtkReader`.  With a shared Qt each copy stays
inside its own `.so` and nobody notices the ODR violation; with a static Qt both
object files land in one link and the three classes are defined twice.  The patch
renames libqtxdg's copies before Qt's header is pulled in, making them genuinely
distinct types.

**liblxqt: no X11, no KF6WindowSystem, no Polkit.**  Upstream guards every X11 bit
with `NOT APPLE`, i.e. it assumes every non-Mac platform has X11; MINIX has none
at all.  The patch gives that guard a name (`LXQT_HAS_X11`) and turns it off.
KWindowSystem is used in exactly one place -- raising an existing instance's
window -- and on MINIX it could only ever be a no-op: there is no X11, and Wayland
does not let one client raise another's window without xdg-activation, which
wlcompd does not implement.  So rather than port a whole KDE Framework to get a
stub, `lxqtsingleapplication.cpp` uses Qt's own `raise()`/`activateWindow()` under
`LXQT_NO_KWINDOWSYSTEM`.  (`lxqtpowerproviders.cpp` only compares
`QGuiApplication::platformName()` at runtime and needs no X11 headers.)

## The Wayland/Qt shim

`external/qt6/cmake/MinixWaylandShim.cmake`, injected with
`-DCMAKE_PROJECT_INCLUDE`, exists because Qt's exported targets name
`Wayland::Client` and `Qt6::GuiPrivate` in their link interfaces without recording
them as propagated dependencies -- so a consumer that merely asks for
`Qt6 COMPONENTS Widgets` fails with "the link interface contains Wayland::Client
but the target was not found".  It also forces static library resolution, which
Qt's `qt.toolchain.cmake` otherwise loses (zlib/freetype/wayland get resolved to
`.so`, and then `-static` fails with "attempted static link of dynamic object").

Use `CMAKE_PROJECT_INCLUDE`, not `CMAKE_PROJECT_INCLUDE_BEFORE`: the latter runs
before compiler detection, so a `find_package` that needs a compiler cannot work
there.

## Consumers must link -static and Q_IMPORT_PLUGIN their platform

A Qt program here has to be `-static` (see above) and, because static Qt plugins
are archives rather than dlopen'd `.so` files, it must register its platform
plugin by hand with `Q_IMPORT_PLUGIN`.  Miss the first and it segfaults before
`main()` with no output at all; miss the second and Qt aborts with "Could not find
the Qt platform plugin".  `minix/commands/lxqtprobe` shows both.

## The C-locale limitation is fixed

Earlier notes here said static binaries on MINIX were stuck in the C locale
(codeset `646`) with no iconv, so GLib could not handle non-ASCII.  That is fixed
in libc -- the citrus encoding and iconv modules are compiled in rather than
dlopen'd; see `external/glib/README.md` and `minix/commands/localeprobe`.  Set
`LANG=en_US.UTF-8` and a static Qt/LXQt program now gets a real UTF-8 locale.

## lxqt-panel

Verified on-target (`drive_lxqtpanel.py`): layer surface `"dock"` on layer 2,
1280x32, pixels committed, and all six enabled plugins loaded.

Run it with:

    XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 \
    QT_QPA_PLATFORM=wayland QT_WAYLAND_SHELL_INTEGRATION=layer-shell \
    lxqt-panel

`QT_WAYLAND_SHELL_INTEGRATION=layer-shell` is not optional: it is how LayerShellQt
selects its QPA shell integration.  lxqt-session normally sets it; there is no
session yet.

### Which plugins, and why only those

Enabled: **mainmenu, fancymenu, quicklaunch, showdesktop, spacer, worldclock,
taskbar, desktopswitch** (the pager).

The taskbar works because wlcompd implements **wlr-foreign-toplevel-management-v1**,
and the pager because it implements **ext-workspace-v1** (four virtual desktops).
The panel's `wlroots` backend speaks both.  That backend is built STATIC and
linked into the panel, and `LXQtPanelApplication` instantiates it directly --
upstream dlopens its WM backends from `*.so`, which a static binary cannot do.

Two independent constraints decide this list.

1. **A plugin has to be linkable statically.**  The panel loads plugins as `.so`
   modules through `QPluginLoader`, and a static binary cannot dlopen -- so on MINIX
   a plugin only exists if it is compiled into the panel.  Upstream already supports
   this (`Plugin::findStaticPlugin`, and the `STATIC_PLUGINS` list), but the table in
   `panel/plugin.cpp` is fixed: only ten plugins can ever be static.  Everything
   outside that table (colorpicker, customcommand, directorymenu, dom, qeyes, ...) is
   module-only and therefore unusable here.  This needs no patch -- upstream already
   puts each of those ten into `STATIC_PLUGINS` when it is enabled.
2. **The rest need things MINIX does not have**: statgrab (cpuload,
   networkmonitor), lm_sensors, ALSA/PulseAudio (volume), libsysstat, X11 (tray,
   kbindicator), or window management (taskbar, desktopswitch -- see
   `external/kf6/README.md`).

### What the panel patch does

- **Static Qt plugin imports** (`panel/minix_static_plugins.cpp`).  lxqt-panel is a
  plain `add_executable()`, so Qt's automatic static-plugin import generation never
  runs and the panel aborts with *"Could not find the Qt platform plugin wayland"*.
  It imports the wayland QPA plugin, LayerShellQt's shell integration, and the icon
  and image plugins.  The executable is linked `-static` like every Qt program here.
- **No X11.**  `KX11Extras` does not exist in a KWindowSystem built without X11.
  Every use of it in the panel is an X11/EWMH operation -- `_NET_WM_STRUT`, dock
  type, virtual desktops -- and none apply on Wayland, where LayerShellQt sets the
  layer, anchors and exclusive zone instead.  The patch gives it a no-op stub
  (`panel/kx11extras_minix.h`) rather than `#ifdef` out a dozen call sites, and
  replaces the three `QNativeInterface::QX11Application` probes -- a type that does
  not exist in a Qt built without XCB -- with a `lxqtPanelIsX11()` helper that is a
  compile-time `false`.
- **One WM backend, linked in.**  Upstream loads them as `.so` plugins, which a
  static binary cannot dlopen.  On MINIX exactly one is built -- `wlroots`, because
  wlcompd implements the protocol it speaks -- as a STATIC library that
  `LXQtPanelApplication` uses directly.  (xcb needs X11; kwin_wayland needs KWin's
  private protocols; wayfire its own.)  It also needs `QT_STATICPLUGIN`: it is a Qt
  plugin, and moc otherwise emits the shared-plugin entry points, which collide with
  the panel's own static plugins.

  Qt must be built with `-DFEATURE_concurrent=ON` for this backend (our Qt recipe
  had it off).

### lxqt-globalkeys

Only the client library and `lxqt-globalkeys-ui` are built; the daemon and its
config UI are skipped.  The daemon grabs keys through X11, and would be pointless
anyway -- Wayland does not let a client grab another's keys, and wlcompd implements
no protocol for it.  Both libraries are STATIC, for the usual reason.

## lxqt-session, and starting the desktop

`lxqt-session` runs the whole thing.  Verified on-target (`drive_session.py`):
`startlxqt` alone brings up wlcompd, a D-Bus session bus, lxqt-session and the
panel -- docked -- with no manual environment setup.

    startlxqt        # that is the entire command

Two upstreams come in with it: `qtxdg-tools` (LXQt's xdg-open backend, a plain
Qt6Xdg consumer) and `xdg-user-dirs` (see `external/xdg-user-dirs/`).

### What the session patch does

- **startlxqt brings the compositor up itself.**  Everywhere else, startlxqt is
  launched by a display manager or from an already-running compositor, so it only
  sets the environment.  MINIX has neither -- you log in on a text console and
  nothing has opened the framebuffer -- so on MINIX startlxqt starts the fb driver
  and wlcompd, waits for the wayland socket, and only then execs lxqt-session.

- **No session-wide `QT_WAYLAND_SHELL_INTEGRATION`.**  It is tempting to export
  `layer-shell` from startlxqt, but that would break every other Qt program in the
  session: LayerShellQt's integration makes *every* window of a process a layer
  surface (its `createShellSurface()` has no xdg-shell fallback).  lxqt-session
  itself failed to start that way.  Only the panel wants layer-shell, so the panel
  asks for it in its own `main()` via `LayerShellQt::Shell::useLayerShell()` --
  that is the one line the panel patch adds to main.cpp.

- **No X11.**  `find_package(X11 REQUIRED)` becomes optional under `LXQT_HAS_X11`,
  and the X11-only config GUI (`lxqt-config-session`, all xrdb/XKB/xset) is skipped.
  Everything the session does with X11 -- key repeat rate, keyboard beep, pointer
  acceleration, button mapping, numlock, waiting for `_NET_SUPPORTING_WM_CHECK` --
  is an X11 device or window-manager setting with no meaning on Wayland, where the
  compositor owns input and *is* the window manager.  Those functions become
  no-ops under `LXQT_SESSION_NO_X11` (the session already had `isWayland` paths, so
  the guards are narrow), and `startWm()` in particular just returns: wlcompd is
  already running, or this process could not be a Wayland client at all.

- **ScreenSaver comes back into liblxqt.**  It had been excluded as X11, but it is
  not X11-only -- its constructor already has a Wayland branch that runs the
  configured lock command, and only the XScreenSaver *query* is X11.  lxqt-session
  and lxqt-leave both need it, so it is built everywhere now with the X11 parts
  behind `LXQT_NO_X11` (see the liblxqt patch); `isScreenSaverLocked()` returns
  false on Wayland, where a client cannot observe the lock state.

- **No autostart entries for things we do not have.**  The globalkeys daemon
  (not built) and xscreensaver (X11) autostart entries are dropped -- an autostart
  entry for a missing binary just makes lxqt-session log a failure at every login.

## qterminal (and qtermwidget)

A terminal emulator for the desktop: `qtermwidget` 2.4.0 is the terminal widget
library, `qterminal` 2.4.0 the application built on it (patches `06-` and `07-`).

### Build

`qtermwidget` first (it installs a `qtermwidget6` CMake package qterminal finds):

    cmake ../qtermwidget-2.4.0 -GNinja \
      -DCMAKE_TOOLCHAIN_FILE=<destdir>/usr/lib/cmake/Qt6/qt.toolchain.cmake \
      -DQT_HOST_PATH=/usr -DMINIX_NO_STAGING=1 -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_PROJECT_INCLUDE=<...>/external/qt6/cmake/MinixWaylandShim.cmake \
      -DMINIX_EXTRA_STANDARD_LIBRARIES="-lutil -lexecinfo -lelf" \
      -DCMAKE_CXX_FLAGS="-D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE" \
      -DQTERMWIDGET_USE_UTEMPTER=OFF -DUSE_UTF8PROC=OFF
    ninja && DESTDIR=<destdir> ninja install

`qterminal` is the same incantation plus `-DCMAKE_PREFIX_PATH=<destdir>/usr` (so it
finds `qtermwidget6`) and the **static-link flags** the executable needs:

    -DCMAKE_EXE_LINKER_FLAGS="-static -fuse-ld=lld -L<destdir>/usr/lib -L<destdir>/lib"

The patches apply with `patch -p1` from the source's parent directory (unlike
`01-`..`05-`, which are `-p0`).

`-lutil` is not optional: qtermwidget's `kpty.cpp` opens the pty with `openpty(3)`,
which lives in `libutil` on MINIX, and it is the application link -- not the static
qtermwidget archive -- that has to resolve it.

**The executable MUST be linked `-static`, and this is not optional either.**
Without it the binary keeps a `PT_INTERP`, so `ld.elf_so` runs at startup and the
dynamic-TLS bug segfaults qterminal before `main()` with *no output at all* (exactly
the failure the panel's notes warn about) -- verified on-target.  The three flags go
together: `-static` drops the interpreter; `-fuse-ld=lld` is required because the
binutils drop removed the cross `ld.bfd`, so the linker must be named explicitly, or
the driver falls back to the *host* `ld.bfd` and cannot find the target libraries;
and the two `-L` paths are needed because `ld.lld` does not derive the static-library
search path from `--sysroot` alone.  (The panel predates the binutils drop, so its
recipe gets away with a bare `-static`.)

### What the patches do, and why

- **STATIC, like everything else.**  qtermwidget hardcodes `add_library(... SHARED)`;
  a shared library cannot hold the non-PIC static Qt, and a dynamic Qt segfaults
  before `main()` anyway (see above).  `SHARED` -> `STATIC`.

- **kpty.cpp is written for glibc/Linux ttys.**  Two MINIX-is-BSD fixes: its login
  accounting `#else` branch calls the glibc `utmp` functions (`pututline`,
  `updwtmp`, `getutline`), but MINIX -- like the BSDs -- records logins through the
  POSIX `utmpx` API (`pututxline`, `updwtmpx`, all in libc), so we define
  `HAVE_UTMPX`/`HAVE_UPDWTMPX` for it; and its `_tcgetattr`/`_tcsetattr` macros fall
  through to Linux `TCGETS`/`TCSETS` ioctls, so `__minix__` joins the BSD branch that
  uses `TIOCGETA`/`TIOCSETA`.

- **No X11.**  qterminal's global-shortcut backend and one config path assume X11.
  The `find_package(X11)` and the qxt backend selection get a
  `CMAKE_SYSTEM_NAME STREQUAL "Minix"` branch that picks
  `src/third-party/qxtglobalshortcut_minix.cpp` -- a no-op backend (Wayland does not
  let a client grab another's keys; wlcompd implements no protocol for it, exactly as
  for lxqt-globalkeys).

- **Static Qt plugin imports** (`src/minix_static_plugins.cpp`).  qterminal is a
  plain `add_executable()`, so Qt's automatic plugin-import generation never runs and
  it aborts with *"Could not find the Qt platform plugin wayland"*.  Only the QPA
  platform plugin and LayerShellQt's integration are imported (and their archives
  linked: `Qt6::QWaylandIntegrationPlugin`, `liblayer-shell.a`) -- the image and
  xdg-shell plugins are already auto-imported by `Qt6::Gui`/`QtWaylandClient`, and
  re-importing them would duplicate their registration symbols.  The layer-shell one
  is what qterminal's drop-down mode needs to become a layer surface on wlcompd.

- **Menu entry and icon.**  `qterminal.desktop` / `qterminal-drop.desktop` (in
  `/usr/share/applications`, both in the set list) give the panel menu its entry.
  Upstream sets `Icon=utilities-terminal`, a freedesktop *theme name* that only
  resolves where a full icon theme is installed -- MINIX has none -- so the patch
  points both at `Icon=qterminal`, the app's own icon.  libqtxdg resolves it through
  `XdgIcon::fromTheme` (a name lookup, never a path load), and its loader enumerates
  a theme only if that theme has an `index.theme`, always falling back to `hicolor`.
  So shipping the icon needs three things, all in the `minix-base` set list:

      /usr/share/icons/hicolor/64x64/apps/qterminal.png   (qterminal installs this)
      /usr/share/icons/hicolor/index.theme                (external/lxqt/hicolor-index.theme)
      /usr/share/applications/qterminal.desktop           (Icon=qterminal)

  `hicolor/index.theme` is not shipped by any package here, so a minimal one is kept
  at `external/lxqt/hicolor-index.theme` and installed to the sysroot.  Verified
  on-target: all three land on the running system with `Icon=qterminal`.  (The dirs
  along that icon path -- `.../icons`, `.../hicolor`, `.../64x64`, `.../apps` -- are
  in the set list *and* METALOG too; a set-list entry alone does not create a dir.)
