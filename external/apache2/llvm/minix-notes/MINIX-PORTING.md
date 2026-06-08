# MINIX port notes — LLVM/clang 13 resync (from clang 3.6)

This tree (external/apache2/llvm, LLVM/clang/lld 13.0.0, imported from NetBSD-10)
replaces the old external/bsd/llvm (clang 3.6.1), which crashed compiling modern
code (e.g. BIND 9.18). Branch: feature/llvm13.

## Done (committed)
- Source import (external/apache2/llvm) + external/apache2/Makefile.
- Host-tool glue: tools/{llvm,llvm-clang,llvm-clang-tblgen,llvm-include,
  llvm-lib,llvm-tblgen} replaced with the clang-13 versions
  (LLVM_TOPLEVEL -> external/apache2/llvm). Dropped tools/llvm-lld, tools/llvm-mcld.
- Build wiring: external/Makefile builds apache2; external/bsd/Makefile no longer
  builds bsd/llvm; etc/mtree/NetBSD.dist.base /usr/include/clang-3.6 -> clang-13.0.
- Removed external/bsd/llvm.

## REMAINING — needs a compiler in the loop (run on capable hardware)

### 1. Port MINIX's clang Driver customizations into clang 13's Minix toolchain
clang 13 split the driver: the Minix bits are now in
  external/apache2/llvm/dist/clang/lib/Driver/ToolChains/Minix.{cpp,h}
and clang 13's STOCK Minix.cpp is generic + has NetBSD-pkgsrc-isms
(`-lCompilerRT-Generic`, `-L/usr/pkg/compiler-rt/lib`, plain crt1/crti/crtbegin/
crtn) that are WRONG for MINIX 3. The MINIX 3.6 logic to re-apply is preserved
verbatim next to this file:
  minix-driver-clang3.6.Tools.cpp.txt      (minix::Assemble + minix::Link jobs)
  minix-toolchain-clang3.6.ToolChains.cpp.txt (Minix ctor file paths, C++ stdlib)
Original also in git history at commit 68a1fa50d3e (external/bsd/llvm).

Key MINIX-specific linker behavior to reproduce in Minix.cpp `Linker::ConstructJob`:
- `--eh-frame-hdr`
- static: `-Bstatic`; shared: `-Bshareable`, `-dynamic-linker /usr/libexec/ld.elf_so`
- ld emulation: i386/x86 -> `-m elf_i386_minix`; x86_64 -> ld default for the
  elf64 minix target (no explicit `-m` in the 3.6 code — VERIFY against the
  current working amd64 ld invocation).
- startfiles (static): crt0.o crti.o crtbegin.o ; (shared): crti.o crtbeginS.o
- libs: C++ -> -lm (+ the `-lmthread` hack, now superseded by real -lpthread);
  -lpthread when -pthread; -lc; then -lgcc / -lgcc_eh / --as-needed -lgcc_s.
  NB: with the new toolchain prefer compiler-rt/libgcc as MINIX actually ships.
- endfiles: crtend.o/crtendS.o, crtn.o
- assembler: GetProgramPath("as"); linker: GetLinkerPath() (GNU ld).
And in `Minix` ctor / include args (from ToolChains.cpp ref): MINIX's per-arch
`=/usr/lib/<arch>` file paths and the `/usr/include/c++`, `/usr/include/g++`
C++ include dirs.

API deltas 3.6 -> 13 to mind while porting: `llvm::make_unique` -> `std::make_unique`;
`Command` ctor now takes `ResponseFileSupport` (see stock Minix.cpp for the 13 form);
helper names (addAssemblerKPIC etc.) may have moved.

### 2. Build the toolchain
`./build.sh -m amd64 -O obj -U -u -j<N> -V MKLLVM=yes tools` (heavy: LLVM 13 host
build). Then a full `distribution`. First compile of Minix.cpp will flag any API
mismatches from the port — fix iteratively.

### 3. distrib sets / mtree loose ends
- distrib/sets/lists/* still reference clang-3.6 paths and the old llvm lib set;
  update to clang-13.0 + the LLVM 13 installed file set (checkflist will tell you
  exactly what's missing/extra).
- /usr/lib/clang/13.0.0/* (compiler-rt/sanitizer dirs) — add to mtree/sets if
  building those (NetBSD-10's NetBSD.dist.base lists them).

### 4. Validate
clang 13 must emit working x86_64-elf64-minix binaries (boot test). Then the
BIND 9.18 work (feature/bind918) unblocks — drop its temporary C11 header shims
(max_align_t/stdnoreturn) since clang 13 provides them.
