.PATH.S: ${.PARSEDIR}
# MINIX clang 3.6 can't assemble VAES/AVX-512 in aesni-gcm-x86_64.S;
# drop it.  ghash-x86_64.S is plain SSE and still works.
.if defined(__MINIX)
MODES_SRCS += ghash-x86_64.o
.else
MODES_SRCS += aesni-gcm-x86_64.o ghash-x86_64.o
.endif
MODESCPPFLAGS = -DGHASH_ASM
.include "../../modes.inc"
