//===- ld.lld.cpp - ELF-only entry point for lld ---------------------------===//
//
// MINIX 3 local.  Upstream's lld driver (lld/tools/lld/lld.cpp) dispatches on
// argv[0] or -flavor to the COFF, Mach-O, MinGW, WebAssembly and ELF linkers,
// and so links against all of them.  We build only the ELF flavour -- nothing
// here links for Windows or Darwin -- so that driver would not link.  This is
// the ELF-only entry point instead.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Driver.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

LLD_HAS_DRIVER(elf)

int main(int argc, char **argv) {
  llvm::InitLLVM x(argc, argv);
  std::vector<const char *> args(argv, argv + argc);

  // exitEarly: we are a standalone linker, so lld may skip its cleanup and
  // exit directly, as upstream's driver does when not used as a library.
  return !lld::elf::link(args, llvm::outs(), llvm::errs(),
                         /*exitEarly=*/true, /*disableOutput=*/false);
}
