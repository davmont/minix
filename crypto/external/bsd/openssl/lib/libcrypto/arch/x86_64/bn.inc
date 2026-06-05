.PATH.S: ${.PARSEDIR}
.PATH.c: ${OPENSSLSRC}/crypto/bn/asm

# MINIX clang 3.6 can't assemble the AVX-512 instructions in
# rsaz-avx2.S / rsaz-avx512.S.  Drop those two and the corresponding
# rsaz_exp_x2.c (which depends on rsaz_avx512_*); the C fallbacks for
# rsaz_exp.c and the basic MONT/GF2m ASM still work.
.if defined(__MINIX)
BN_SRCS = x86_64-gf2m.S x86_64-mont.S x86_64-mont5.S rsaz-x86_64.S x86_64-gcc.c
.else
BN_SRCS = x86_64-gf2m.S x86_64-mont.S x86_64-mont5.S rsaz-x86_64.S rsaz-avx2.S rsaz-avx512.S x86_64-gcc.c
.endif
CPPFLAGS += -DOPENSSL_BN_ASM_MONT -DOPENSSL_BN_ASM_MONT5 -DOPENSSL_BN_ASM_GF2m
.include "../../bn.inc"
