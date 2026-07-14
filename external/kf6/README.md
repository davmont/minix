# KDE Frameworks on MINIX

Three KDE components, needed by `lxqt-panel` (see `external/lxqt/`):

    extra-cmake-modules 6.10.0   build-time CMake modules (ECM)
    kwindowsystem 6.10.0         KF6::WindowSystem -- lxqt-panel requires it
    layer-shell-qt 6.3.5         LayerShellQt -- how the panel actually docks

`layer-shell-qt` is the interesting one: it drives **wlr-layer-shell** through
QtWayland's private API, and that is precisely the protocol wlcompd implements.
It is what puts the panel on screen.

## Build

`extra-cmake-modules` is **built natively**, not cross-compiled -- it ships only
CMake modules -- and installed into the sysroot so the cross builds find it:

    cmake ../extra-cmake-modules-6.10.0 -GNinja -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF
    DESTDIR=<destdir> ninja install

The other two are cross-compiled with the usual MINIX incantation (toolchain file,
`MINIX_NO_STAGING=1`, plain prefix + `DESTDIR`, the shim via
`CMAKE_PROJECT_INCLUDE`), plus:

    -DCMAKE_PREFIX_PATH="<destdir>/usr;<destdir>/usr/share/ECM"
    -DKF_IGNORE_PLATFORM_CHECK=TRUE
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF

and, for kwindowsystem:

    -DKWINDOWSYSTEM_X11=OFF -DKWINDOWSYSTEM_WAYLAND=OFF -DKWINDOWSYSTEM_QML=OFF

## KF_IGNORE_PLATFORM_CHECK

ECM refuses to configure on an unknown platform: *"Your current platform 'Minix'
is not supported. The list of supported platorms is
'Linux;FreeBSD;macOS;Windows;Android'"*.  KDE provides the escape hatch itself --
that is what the flag is for.

## KWindowSystem is a stub here, on purpose

Both backends are off.  X11 does not exist on MINIX, and the Wayland backend needs
`plasma-wayland-protocols` -- KWin's private window-management protocol -- which
wlcompd does not speak.  So `KWindowSystem` builds with no platform plugin and its
window-management calls become no-ops.

That is honest rather than lossy: **no Wayland compositor lets one client enumerate
or raise another's windows** without an explicit protocol, and wlcompd implements
none.  A full KWindowSystem port would compile down to the same no-op.  The visible
consequence is that lxqt-panel logs *"Could not create a backend for window
management operations. Falling back to dummy backend"* and the taskbar and
pager plugins have nothing to show -- which is why they are not built.

To make them work, wlcompd would need **wlr-foreign-toplevel-management-v1**; the
panel already has a `wlroots` backend that speaks exactly that.

## The layer-shell-qt patch

Two changes, both forced by the static Qt:

- **Drop the QML bindings.**  Upstream requires `Qt6Qml`, which would mean porting
  qtdeclarative.  Nothing needs it: the QML module lives entirely in
  `src/declarative`, and lxqt-panel uses the C++ `LayerShellQt::Interface`.
- **Build the shell-integration plugin STATIC, with `QT_STATICPLUGIN`.**  Qt is
  linked statically on MINIX, so its QPA plugins are archives that the application
  imports with `Q_IMPORT_PLUGIN` rather than `.so` files it dlopens.  The define is
  what makes moc emit `qt_static_plugin_QWaylandLayerShellIntegrationPlugin()` --
  without it the plugin builds fine and then fails to link into anything importing
  it.

A consumer must also set `QT_WAYLAND_SHELL_INTEGRATION=layer-shell` at runtime;
that is how LayerShellQt selects its QPA shell integration, and lxqt-session
normally does it.
