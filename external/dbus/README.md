# D-Bus on MINIX

dbus 1.16.2 cross-compiled for MINIX/amd64.  LXQt needs a session bus, so this
is the next piece after Qt 6 (see `external/qt6/`).

Verified on-target: `dbus-launch` starts a session bus, and a client completes
the authentication handshake and gets replies to real method calls --
`org.freedesktop.DBus.GetId` returns a bus id, `ListNames` lists the bus.
Starting the daemon proves very little on its own; it can come up and still be
unable to serve anyone, which is exactly what happened at first (see below).

## Why D-Bus is not vendored here

Upstream is unmodified: no patches were needed.  All that MINIX requires is the
right *configuration*, which is the recipe below.

## Build

    curl -LO https://dbus.freedesktop.org/releases/dbus/dbus-1.16.2.tar.xz
    tar xJf dbus-1.16.2.tar.xz
    mkdir build && cd build

    D=<destdir.amd64>
    cmake ../dbus-1.16.2 -GNinja \
      -DCMAKE_TOOLCHAIN_FILE=<qt6 work area>/minix-toolchain.cmake \
      -DMINIX_NO_STAGING=1 \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_INSTALL_SYSCONFDIR=/etc \
      -DCMAKE_INSTALL_LOCALSTATEDIR=/var \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_STANDARD_LIBRARIES="-lpthread -lexecinfo" \
      -DCMAKE_C_FLAGS="-D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE -DHAVE_UNPCBID=1" \
      -DDBUS_BUILD_TESTS=OFF -DDBUS_ENABLE_XML_DOCS=OFF \
      -DDBUS_ENABLE_DOXYGEN_DOCS=OFF -DDBUS_WITH_GLIB=OFF \
      -DENABLE_SYSTEMD=OFF -DDBUS_SESSION_SOCKET_DIR=/tmp
    ninja
    DESTDIR=$D ninja install

Then refresh METALOG and rebuild the sets and the image.

## The four things that are not obvious

**`-DHAVE_UNPCBID=1` is what makes the bus able to serve anyone.**  D-Bus
authenticates a client over the unix socket with SASL EXTERNAL, which means it
must learn the peer's credentials.  It has four ways to do that; the one MINIX
has (inherited from NetBSD) is `getsockopt(LOCAL_PEEREID)` filling a
`struct unpcbid`, and `minix/net/uds` does implement it.  But that path is
guarded by `HAVE_UNPCBID`, and *only D-Bus's autotools build ever tests for it*
-- the CMake build never defines it.  So the CMake build silently compiles with
no credential mechanism at all: the daemon starts, accepts the connection, and
then never completes the handshake.  The client just sits there until it times
out with "Did not receive a reply", which does a good job of looking like a
hang rather than a missing feature.

**Repeat the `-D__minix` defines in `CMAKE_C_FLAGS`.**  Passing `CMAKE_C_FLAGS`
on the command line *overrides* the toolchain's `CMAKE_C_FLAGS_INIT`, so the
defines it supplies are lost.  Without them `<sys/errno.h>` never defines
`_SIGN` and every errno constant fails to parse.

**`-DMINIX_NO_STAGING=1`, and a plain prefix plus `DESTDIR`.**  D-Bus installs
into `/etc` and `/var` as well as `/usr`.  An absolute sysconfdir escapes a
`CMAKE_STAGING_PREFIX` and lands on the *build host*, so staging cannot be used
here.  Note that setting `CMAKE_STAGING_PREFIX` to the empty string is not an
opt-out: CMake then treats `""` as the install destination and everything lands
in `$DESTDIR/bin` instead of `$DESTDIR/usr/bin`.  Hence the explicit flag.

**`/dev/urandom` must work.**  D-Bus generates its bus GUID from it and refuses
to start otherwise.  That used to fail here -- see `etc/rc.minix`, which now
starts the `random` driver at boot.


## A static libdbus-1.a, for linking into Qt

The recipe above builds the daemon and a shared `libdbus-1.so` -- which is what the
daemon binaries use.  But Qt has to be linked *statically* on MINIX and, with
`-dbus-linked`, needs a static `libdbus-1.a` on the link line (see
`external/qt6/README.md`).  dbus hardcodes `add_library(dbus-1 SHARED)`, so a second
build with `patches/01-dbus-static-lib.patch` (SHARED -> STATIC) and
`-DBUILD_SHARED_LIBS=OFF` produces the archive:

    patch -p1 < patches/01-dbus-static-lib.patch
    cmake ... -DBUILD_SHARED_LIBS=OFF -DDBUS_SESSION_SOCKET_DIR=/tmp   # same flags as above
    ninja dbus-1
    cp lib/libdbus-1.a <destdir>/usr/lib/

The patch is only for this static build; the daemon build stays shared.
