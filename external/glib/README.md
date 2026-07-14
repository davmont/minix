# GLib on MINIX

GLib 2.80.5 cross-compiled for MINIX/amd64.  It is here because libqtxdg's
mime-apps backend is GIO (`GDesktopAppInfo`) -- every libqtxdg 4.x requires GLib,
so the LXQt stack cannot avoid it.

Verified on-target by `minix/commands/glibprobe`: the base-directory lookups, the
GObject type system, a `GMainContext` iteration, and `g_app_info_get_all()` --
i.e. GIO walking the desktop-file databases.  Also exercised through libqtxdg by
`minix/commands/lxqtprobe`.

## Build

    curl -LO https://download.gnome.org/sources/glib/2.80/glib-2.80.5.tar.xz
    tar xJf glib-2.80.5.tar.xz
    patch -p0 < patches/01-glib-minix.patch          # meson.build: see below

    export PKG_CONFIG_LIBDIR=<destdir>/usr/lib/pkgconfig
    export PKG_CONFIG_SYSROOT_DIR=<destdir>
    meson setup build glib-2.80.5 --cross-file minix-cross.ini \
        --default-library=static \
        -Dtests=false -Dintrospection=disabled -Dman-pages=disabled \
        -Dnls=disabled -Dlibmount=disabled -Dselinux=disabled -Dxattr=false
    ninja -C build
    DESTDIR=<destdir> ninja -C build install

Needs pcre2 in the sysroot (build it with `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`
if anything downstream will go into a shared object).  zlib, libffi and libintl
are already there; iconv comes from MINIX's libc.

## The patch: pthread_getaffinity_np

Meson only checks that the *symbol* exists.  MINIX has `pthread_getaffinity_np`
(inherited from NetBSD), but it takes a `cpuset_t`, while `gthread.c` calls it
the glibc way -- `cpu_set_t` with `CPU_ZERO`/`CPU_COUNT`, none of which exist
here.  So GLib configured the feature in and then failed to compile.  The patch
makes the check compile the actual usage, which sends MINIX down the
`sysconf(_SC_NPROCESSORS_ONLN)` branch instead.

## Why the cross file says system = 'minix'

Not `'netbsd'`.  GLib picks its file-monitor backend by probing for functions
rather than by OS name, and MINIX has neither inotify nor kqueue, so it correctly
falls back to polling either way -- but claiming to be NetBSD would switch on
other BSD-only assumptions elsewhere in the tree.

Note also that meson does **not** pass `--sysroot` to the compiler on its own:
unlike CMake's `CMAKE_SYSROOT`, the cross file's `sys_root` only feeds
pkg-config.  It is in `c_args` explicitly; without it not even `<stddef.h>` is
found and every probe, down to `sizeof(char)`, fails.

## The C-locale / iconv problem, and its fix

GLib used to flood stderr with `Conversion from character set '646' to 'UTF-8' is
not supported`.  That was not a GLib bug: citrus implemented both the LC_CTYPE
encoding handlers and the iconv converters as **dlopen'd modules** under
`/usr/lib/i18n`, and MINIX links statically, so no static program could load any
of them.  `setlocale(LC_CTYPE, "en_US.UTF-8")` silently stayed in the C locale
and every `iconv_open()` failed.

Fixed in libc: the modules that matter are now compiled into libc and looked up
in a table before dlopen is considered (`lib/libc/citrus/citrus_module_builtin.c`),
and the `_I18N_DYNAMIC` gates in `citrus_ctype.c`/`citrus_stdenc.c` -- which had
compiled a stub for static libc that accepted only the default encoding -- are
gone.  Verified by `minix/commands/localeprobe`.

Anything built before that fix must be relinked to pick it up.
