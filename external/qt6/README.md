# Qt 6 on MINIX

Qt 6.11.1 (qtbase) cross-compiled for MINIX/amd64, rendering with its raster
engine into `wl_shm` buffers on `wlcompd`.  No GPU is involved anywhere -- which
is the point, since MINIX has no DRM, no Mesa and no EGL.  That is also why
KWin/Plasma, and every wlroots compositor (Sway, Wayfire, river, Cage), cannot
be ported: they need a GPU stack.  Qt's software path does not.

Verified on-target by `minix/commands/qtprobe`: QtCore, the meta-object system,
the raster paint engine, the poll(2) event dispatcher, and a QWidget window
whose pixels arrive at the compositor over wl_shm.

## Why Qt is not vendored here

qtbase is ~330 MB of source.  Rather than carry it in-tree, this directory holds
everything MINIX-specific -- the patches, the CMake platform and toolchain
files, and the mkspec -- and the recipe below fetches the rest.

## Build

    QT=6.11.1                       # must match the HOST Qt exactly: the cross
                                    # build needs host moc/rcc/uic of the same
                                    # version (QT_HOST_PATH below)
    curl -LO https://download.qt.io/official_releases/qt/6.11/$QT/submodules/qtbase-everywhere-src-$QT.tar.xz
    tar xJf qtbase-everywhere-src-$QT.tar.xz && cd qtbase-everywhere-src-$QT
    for p in <this dir>/patches/*.patch; do patch -p1 < "$p"; done
    cp -r <this dir>/mkspecs/minix-clang mkspecs/
    cd ..

    cmake -S qtbase-everywhere-src-$QT -B build-minix -GNinja \
      -DCMAKE_TOOLCHAIN_FILE=<this dir>/minix-toolchain.cmake \
      -DQT_HOST_PATH=/usr \
      -DQT_QMAKE_TARGET_MKSPEC=minix-clang \
      -DCMAKE_INSTALL_PREFIX=<destdir>/usr \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DINPUT_opengl=no -DINPUT_egl=no -DINPUT_xcb=no -DINPUT_dbus=no \
      -DINPUT_glib=no -DINPUT_icu=no -DINPUT_fontconfig=no \
      -DINPUT_harfbuzz=qt -DINPUT_pcre=qt -DINPUT_libpng=qt -DINPUT_libjpeg=qt \
      -DINPUT_zlib=system -DINPUT_freetype=system \
      -DFEATURE_libudev=OFF -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF \
      -DFEATURE_sql=OFF -DFEATURE_testlib=OFF -DFEATURE_network=OFF \
      -DFEATURE_printsupport=OFF -DFEATURE_concurrent=OFF \
      -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    ninja -C build-minix && ninja -C build-minix install

Qt bundles pcre2, harfbuzz, libpng, libjpeg and md4c, so none of those need
porting.  fontconfig is optional (Qt falls back to a FreeType font database).
libinput and udev are not needed at all: a Qt *client* never touches them --
wlcompd owns the input devices.

## Two things that are not optional

**Link statically.**  A dynamically linked Qt segfaults during startup, before
main() is reached.  Qt leans heavily on `thread_local`, and dynamic TLS is the
corner of `ld.elf_so` least exercised on MINIX; a static link uses local-exec
TLS and sidesteps it entirely.  It also matches how the rest of the system is
built.  `-static`, and point the Freetype/zlib/Wayland cache entries at the `.a`
files (a static `libwayland-client.a` records no dependency of its own, so
`-lffi` must come after it -- the toolchain appends it last).

**MINIX is its own platform, not NetBSD.**  It is tempting to claim to be
NetBSD, since MINIX has a NetBSD userland.  Do not: Qt gates its kqueue
file-system watcher on `APPLE OR FREEBSD OR NETBSD OR OPENBSD`, and MINIX has no
kqueue at all (`kevent` and `kqueue` are both in libc's `MISSING_SYSCALLS` --
the same gap that made libwayland need a poll(2) emulation here).  Being a
platform of our own is what keeps Qt on the portable paths.  `Q_OS_BSD4` *is*
claimed, and correctly: it selects `arc4random_buf`, `d_type` in dirent, and BSD
socket semantics, all of which MINIX has.

## The patches

  01  qsystemdetection.h  Add Q_OS_MINIX.  Qt otherwise refuses to build at all
                          ("Qt has not been ported to this OS").
  02  qtypes.h            C-mode Qt needs ptrdiff_t but only includes <assert.h>,
                          relying on glibc dragging in <stddef.h>.  Ask for it.
  03  forkfd.c            MINIX has no SA_SIGINFO.  Nothing is lost: the handler
                          ignores the siginfo it is passed and calls waitid()
                          itself, so a plain handler does the same job.
  04  brg_endian.h        MINIX's endian macros are in <sys/endian.h>, as on the
                          BSDs; without this it asks for glibc's <endian.h>.
  05  qlockfile_unix.cpp  Q_OS_BSD4 does not imply kinfo_proc/KERN_PROC, which
                          MINIX lacks.
  06  qthread_unix.cpp    MINIX follows NetBSD: pthread_setname_np() is the
                          three-argument, printf-style form.
  07  qstorageinfo_unix.cpp  MINIX has statvfs, not statfs -- Qt already carves
                          NetBSD out of Q_OS_BSD4 for this; MINIX joins it.
  08  qwaylandshmbackingstore.cpp  The important one.  With no memfd_create Qt
                          falls back to a temp file and mmaps it MAP_SHARED --
                          which MINIX cannot do for a plain file at all (a file
                          mapping copy-on-writes into private pages, so the
                          compositor would never see what the client painted).
                          Use shm_open(), whose mmap is routed to the IPC server
                          and yields genuinely shared pages.  Without this Qt
                          connects, creates a surface, and never produces a
                          single pixel.

Several MINIX libc gaps were found by this port and fixed in the tree rather
than worked around here -- fdatasync, madvise/posix_madvise and dup3 were all
*declared* and never implemented; <assert.h> did not define C11's
static_assert; and __cxa_thread_atexit was missing, without which nothing using
a thread_local destructor could link.  See the commit "libc: fill in the POSIX
and C11 gaps that Qt found".
