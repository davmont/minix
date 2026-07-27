# CMake cross-compilation toolchain for MINIX 3 / amd64.
#
# Targets the MINIX sysroot that build.sh populates (destdir.amd64) using the
# tree's cross-clang wrappers.
#
# The three -D__minix defines are NOT optional decoration.  <sys/errno.h> gates
# its definition of _SIGN on "#if defined(__minix)", and every errno constant is
# spelled (_SIGN 96) -- so without them _SIGN is undefined and the C library
# headers fail to parse.  The MINIX build supplies these through bsd.own.mk; a
# CMake build has to supply them itself.

set(CMAKE_SYSTEM_NAME Minix)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 3)

# CMake ships no Platform/Minix.cmake; ours lives here.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Sysroot and cross tools come from the environment so this file is not tied to
# one machine: releasetools/build-desktop.sh exports MINIX_SYSROOT (=DESTDIR) and
# MINIX_TOOLS (=TOOLDIR/bin).  A manual build can export them too; the fallbacks
# are only a convenience default for one particular checkout.
if(NOT DEFINED MINIX_SYSROOT)
    if(DEFINED ENV{MINIX_SYSROOT})
        set(MINIX_SYSROOT "$ENV{MINIX_SYSROOT}")
    else()
        set(MINIX_SYSROOT "/home/david/Documentos/Code/build/destdir.amd64")
    endif()
endif()
if(NOT DEFINED MINIX_TOOLS)
    if(DEFINED ENV{MINIX_TOOLS})
        set(MINIX_TOOLS "$ENV{MINIX_TOOLS}")
    else()
        set(MINIX_TOOLS "/home/david/Documentos/Code/build/tooldir.Linux-x86_64/bin")
    endif()
endif()

set(CMAKE_SYSROOT "${MINIX_SYSROOT}")
# Only if the project has not chosen for itself.  A project that installs into
# /etc or /var (D-Bus does both) needs a plain prefix plus DESTDIR instead, and
# an unconditional set() here would silently override its -D and bake build-host
# paths into the installed binaries.
# A project that also installs into /etc or /var (D-Bus does both) cannot use a
# staging prefix: an absolute sysconfdir escapes it and lands on the build host.
# Such a project passes -DMINIX_NO_STAGING=1 and installs with DESTDIR instead.
# Setting CMAKE_STAGING_PREFIX to the empty string does NOT work as an opt-out --
# CMake then treats "" as the install destination and everything lands in
# DESTDIR/bin rather than DESTDIR/usr/bin.
if(NOT MINIX_NO_STAGING)
    set(CMAKE_STAGING_PREFIX "${MINIX_SYSROOT}/usr")
endif()

set(CMAKE_C_COMPILER   "${MINIX_TOOLS}/x86_64-elf64-minix-clang")
set(CMAKE_CXX_COMPILER "${MINIX_TOOLS}/x86_64-elf64-minix-clang++")
set(CMAKE_AR           "${MINIX_TOOLS}/x86_64-elf64-minix-ar")
set(CMAKE_RANLIB       "${MINIX_TOOLS}/x86_64-elf64-minix-ranlib")
set(CMAKE_NM           "${MINIX_TOOLS}/x86_64-elf64-minix-nm")
set(CMAKE_OBJDUMP      "${MINIX_TOOLS}/x86_64-elf64-minix-objdump")
set(CMAKE_STRIP        "${MINIX_TOOLS}/x86_64-elf64-minix-strip")

# Use the MINIX linker (lld) and the sysroot libraries when linking executables
# or shared objects.  Without -fuse-ld=lld the cross clang falls back to the
# build host's /usr/bin/ld.bfd, which cannot link MINIX objects or find
# libc++/libc in the sysroot; -L points it at them.
set(_MINIX_LINK "-fuse-ld=lld -L${MINIX_SYSROOT}/usr/lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_MINIX_LINK}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_MINIX_LINK}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_MINIX_LINK}")

set(MINIX_DEFINES "-D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE")

set(CMAKE_C_FLAGS_INIT   "${MINIX_DEFINES}")
set(CMAKE_CXX_FLAGS_INIT "${MINIX_DEFINES}")

# libc++ is the C++ runtime, and libc carries the unwinder; see cxxprobe.
# Appended last on the link line, which is where a static libffi has to go:
# libwayland-client.a calls ffi_call() but, being an archive, records no
# dependency of its own, so ffi must be resolved after it.
# -lpthread is where MINIX keeps the POSIX semaphores (sem_wait/sem_post/sem_open);
# libc.a has none of them.  QtCore's QSystemSemaphore calls them, so a consumer
# that pulls it in fails to link without this.  Appended last, which is where it
# has to be: libQt6Core.a is an archive, so its references resolve left-to-right.
# MINIX keeps backtrace(3) in libexecinfo, not libc; GLib's gbacktrace.c calls it.
#
# MINIX_EXTRA_STANDARD_LIBRARIES lets a project append libraries that must be
# resolved at the very end of the link.  Needed because everything here is a
# static archive, so a library that records no dependencies of its own -- GLib
# found via find_library(), say -- leaves its callees (glib__private__,
# bindtextdomain) unresolved unless they follow it on the command line.  The
# extras go FIRST so that -lffi still lands after -lgobject-2.0, which needs it.
# -ldbus-1: Qt6DBus is built "linked" (FEATURE_dbus_linked), i.e. it calls into
# libdbus directly rather than dlopen'ing it -- because a static binary cannot
# dlopen, so the runtime-loaded default leaves Qt's D-Bus permanently
# disconnected (isConnected()==false, registerService() fails).  libdbus-1.a has
# to be on the final link line of anything that pulls in Qt6DBus; as a static
# archive it costs nothing when unreferenced.  Before -lpthread, which it needs.
set(CMAKE_CXX_STANDARD_LIBRARIES
    "${MINIX_EXTRA_STANDARD_LIBRARIES} -lffi -lc++ -lm -ldbus-1 -lpthread -lexecinfo -lelf")
set(CMAKE_C_STANDARD_LIBRARIES
    "${MINIX_EXTRA_STANDARD_LIBRARIES} -lffi -lm -lpthread -lexecinfo")

# pkg-config must answer for the TARGET, not the build host: without this it
# would happily report the host's wayland-client and Qt would link the wrong
# library -- or, as happened here, fail to find Wayland::Client at all.
set(ENV{PKG_CONFIG_LIBDIR} "${MINIX_SYSROOT}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MINIX_SYSROOT}")
set(ENV{PKG_CONFIG_PATH} "")

# Look for headers and libraries in the sysroot, never on the build host.
# Prefer static libraries.  MINIX links its programs statically, and a
# dynamically linked Qt segfaults during startup before main() -- Qt leans on
# thread_local, and dynamic TLS is the part of ld.elf_so least likely to be
# exercised here.  Linking statically avoids dynamic TLS entirely (local-exec)
# and matches how the rest of the system is built.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

set(CMAKE_FIND_ROOT_PATH "${MINIX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# MINIX links statically by default and has no working shared-object story for
# these libraries yet; a try_compile that links a shared lib would fail for
# reasons unrelated to what is being tested.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
