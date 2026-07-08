.PATH.S: ${.PARSEDIR}
.PATH.c: ${OPENSSLSRC}/crypto/bn/asm
BN_SRCS = x86_64-gf2m.S x86_64-mont.S x86_64-mont5.S x86_64-gcc.c
.if defined(__MINIX)
# MINIX: RSAZ_ENABLED is off (dist/crypto/bn/rsaz_exp.h) and the in-tree
# binutils 2.34 gas cannot assemble the AVX-IFMA variants; keep only the
# plain rsaz-x86_64.S like the 3.0 reachover did.
BN_SRCS += rsaz-x86_64.S
.else
BN_SRCS += \
rsaz-2k-avx512.S \
rsaz-2k-avxifma.S \
rsaz-3k-avx512.S \
rsaz-3k-avxifma.S \
rsaz-4k-avx512.S \
rsaz-4k-avxifma.S \
rsaz-avx2.S \
rsaz-x86_64.S \

.endif # defined(__MINIX)

# Needs newer clang/llvm
.if ${HAVE_LLVM:U} == "yes"
COPTS.rsaz-2k-avx512.S  += -fno-integrated-as
COPTS.rsaz-2k-avxifma.S += -fno-integrated-as
COPTS.rsaz-3k-avx512.S  += -fno-integrated-as
COPTS.rsaz-3k-avxifma.S += -fno-integrated-as
COPTS.rsaz-4k-avx512.S  += -fno-integrated-as
COPTS.rsaz-4k-avxifma.S += -fno-integrated-as
.endif

CPPFLAGS += -DOPENSSL_BN_ASM_MONT -DOPENSSL_BN_ASM_MONT5 -DOPENSSL_BN_ASM_GF2m
.include "../../bn.inc"
