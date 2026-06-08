.PATH.S: ${.PARSEDIR}
# MINIX clang 3.6 can't assemble VAES/AVX-512 instructions in
# aesni-{mb,sha1,sha256}-x86_64.S.  Drop those three; the basic AESNI
# (aesni-x86_64.S), AES, BSAES, and VPAES ASM stay.
.if defined(__MINIX)
AES_SRCS += \
aes-x86_64.S \
aesni-x86_64.S \
bsaes-x86_64.S \
vpaes-x86_64.S
.else
AES_SRCS += \
aes-x86_64.S \
aesni-mb-x86_64.S \
aesni-sha1-x86_64.S \
aesni-sha256-x86_64.S \
aesni-x86_64.S \
bsaes-x86_64.S \
vpaes-x86_64.S
.endif

AESCPPFLAGS = -DAES_ASM -DVPAES_ASM -DBSAES_ASM
AESNI = yes
.include "../../aes.inc"
