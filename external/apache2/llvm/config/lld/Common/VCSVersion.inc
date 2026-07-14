/* Curated replacement for the VCSVersion.inc CMake generates for lld.
 * The revision is COMMON_REVISION from ../../../Makefile.inc: lld is taken
 * from the same llvm-project commit as the LLVM libraries it links against. */
#define LLD_REPOSITORY "git@github.com:llvm/llvm-project.git"
#define LLD_REVISION "a255c1ed36a1d06f79bd2633ba9f8d900153007c"
