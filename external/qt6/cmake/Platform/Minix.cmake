# CMake platform definition for MINIX 3.
#
# CMake ships no Platform/Minix.cmake, so without this a cross build with
# CMAKE_SYSTEM_NAME=Minix fails before it starts.  Modelled on Platform/NetBSD:
# MINIX has a NetBSD userland, an ELF toolchain and a GNU-style linker, so the
# flags are the same.
#
# Naming it Minix rather than reusing NetBSD is not cosmetic.  Qt gates its
# kqueue file-system watcher on "APPLE OR FREEBSD OR NETBSD OR OPENBSD", and
# MINIX has no kqueue at all -- claiming to be NetBSD would build that watcher
# and fail.  Being our own platform is what keeps Qt on the portable paths.

set(CMAKE_DL_LIBS "")
set(CMAKE_C_COMPILE_OPTIONS_PIC "-fPIC")
set(CMAKE_C_COMPILE_OPTIONS_PIE "-fPIE")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "-fPIC")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "-fPIE")

set(CMAKE_SHARED_LIBRARY_C_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP ":")
set(CMAKE_SHARED_LIBRARY_RPATH_ORIGIN_TOKEN "\$ORIGIN")
set(CMAKE_SHARED_LIBRARY_RPATH_LINK_C_FLAG "-Wl,-rpath-link,")
set(CMAKE_SHARED_LIBRARY_SONAME_C_FLAG "-Wl,-soname,")
set(CMAKE_EXE_EXPORTS_C_FLAG "-Wl,--export-dynamic")

set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_SONAME_CXX_FLAG "-Wl,-soname,")

set(CMAKE_LINK_GROUP_USING_RESCAN "LINKER:--start-group" "LINKER:--end-group")
set(CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED TRUE)

include(Platform/UnixPaths)
