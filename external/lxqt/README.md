# LXQt libraries on MINIX

`libqtxdg` 4.4.0, `liblxqt` 2.4.0, `lxqt-globalkeys` 2.4.0, `lxqt-menu-data` 2.4.0
and **`lxqt-panel` 2.4.1**, cross-compiled for MINIX/amd64.

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
taskbar**.

The taskbar works because wlcompd implements **wlr-foreign-toplevel-management-v1**
and the panel's `wlroots` backend speaks it.  That backend is built STATIC and
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
