# Building MINIX/amd64 with a host clang 22 toolchain

## Background

`external/apache2/llvm/dist` is an LLVM/clang/lld **13.0.0** import, but its
`libcxx` subtree was bumped to **libc++ 22** (`_LIBCPP_VERSION 220107`).
libc++ 22 hard-requires clang >= 20, so the in-tree clang 13 can no longer
compile any C++ in the tree (libunwind, libc++ itself, atf, the shipped LLVM,
...).  A real `release` rebuild therefore fails; it only "worked" before against
a stale cached object tree.

Rather than import a full LLVM 22, we drive the cross-build with a **host clang
>= 20** as an `EXTERNAL_TOOLCHAIN`, reusing the in-tree binutils and the libc++
22 headers.

## How to build

Pass `build.sh -c <clang>` (host clang must be >= 20):

```sh
sh build.sh -c clang -j16 -mamd64 -O ../build -U release
```

`-c` makes `build.sh` (re)generate the external toolchain under
`<objdir>/ext-tc` and set `EXTERNAL_TOOLCHAIN` to it automatically.  The argument
is the host clang command or path, so `-c clang-22` or `-c /opt/llvm/bin/clang`
work too.

You can also drive it by hand if you prefer — generate the toolchain once and
point `EXTERNAL_TOOLCHAIN` at it:

```sh
sh external/apache2/llvm/mkclang22toolchain.sh \
    -t ../build/tooldir.<host>-x86_64 -d ../build/destdir.amd64 -o ../build/ext-tc
EXTERNAL_TOOLCHAIN=$PWD/../build/ext-tc \
    sh build.sh -j16 -mamd64 -O ../build -U release
```

The external toolchain lives in its own directory, so the `tools` phase
rebuilding/installing the in-tree clang 13 into `TOOLDIR` never disturbs it (no
`-u` required).

## What the wrapper does

The generated `${TRIPLE}-clang` wrapper (see `mkclang22toolchain.sh` for the
full rationale) routes to the host clang and:

* **compiles** with `--target=x86_64-elf64-minix` (correct MINIX predefined
  macros; libc++ 22's platform selection already has `|| defined(__minix)`
  patches that pick its NetBSD locale backend);
* **links** with `--target=x86_64-unknown-netbsd` so clang invokes `ld`
  directly and honours `-nodefaultlibs` (the Minix link driver delegates to
  `gcc` and drops it, which breaks the `libminc`-only servers and pulls host
  libc);
* adds `-B<toolchain>` (in-tree binutils) and `-B<destdir>/usr/lib` (so
  `--print-file-name=crt*.o` resolves), and `-isystem <sysroot>/usr/include/c++`
  for C++ (MINIX installs libc++ headers flat, not under `/v1`).

## Companion in-tree fixes

These are committed source changes the migration also needs:

* `share/mk/bsd.sys.mk` -- `-Wno-unknown-warning-option` so the `-Wno-error=`
  list is tolerant of warning names unknown to whatever clang is active.
* `external/bsd/libc++/lib/Makefile` -- `-D_NETBSD_SOURCE` so the library build
  sees the POSIX `*_l` wide-ctype functions its NetBSD locale backend uses.
* `external/apache2/llvm/Makefile.inc` -- gate the target LLVM C++-modules build
  behind `LLVM_USE_CXX_MODULES` (off by default; clang22+libc++22 miscompile the
  module form of `promote.h`).
* `external/apache2/llvm/dist/llvm/lib/Support/Unix/Path.inc` and
  `.../config/llvm/Config/config.h.in` -- MINIX libc gaps in the **target** LLVM
  build: use `f_flag` (not `f_flags`) for `__minix`, and undef
  `HAVE_POSIX_FALLOCATE` / `HAVE_SIGALTSTACK` (MINIX libc lacks them).
