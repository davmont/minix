# Injected into third-party projects via -DCMAKE_PROJECT_INCLUDE_BEFORE.
#
# Qt's Qt6::WaylandClient (and the Wayland platform plugins that Qt6Gui pulls in)
# name Wayland::Client in their link interface, but Qt does not record Wayland as
# a propagated dependency -- so merely calling find_package(Qt6 COMPONENTS Widgets)
# fails with "the link interface contains Wayland::Client but the target was not
# found".  Every consumer has to create that target itself.
#
# Qt ships the find module it used; point CMake at it and run it.  Doing this from
# CMAKE_PROJECT_INCLUDE_BEFORE means it happens right after project(), before the
# project's own find_package(Qt6 ...) -- and without patching the project.

set(MINIX_SYSROOT "/home/david/Documentos/Code/build/destdir.amd64")

# Prefer static libraries.  Our own toolchain file sets this, but a project that
# goes through Qt's qt.toolchain.cmake (which chainloads ours) does not keep it,
# so zlib/freetype/wayland get resolved to .so -- and then -static fails with
# "attempted static link of dynamic object".  Everything Qt here must be static:
# a dynamically linked Qt segfaults before main() in ld.elf_so's dynamic TLS.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

list(APPEND CMAKE_MODULE_PATH
     "${MINIX_SYSROOT}/usr/lib/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules")

find_package(Wayland COMPONENTS Client Cursor)

# Same class of problem one level up: libqtxdg's exported Qt6Xdg target names
# Qt6::GuiPrivate in its link interface (it forks Qt's private icon loader), but
# a consumer such as liblxqt only asks for Qt6 Widgets/DBus.  Create the target
# here so the consumer's find_package(Qt6Xdg) does not fail on it.
find_package(Qt6GuiPrivate REQUIRED)
