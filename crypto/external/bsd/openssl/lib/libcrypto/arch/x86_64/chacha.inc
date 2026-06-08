.PATH.S: ${.PARSEDIR}

# MINIX ships clang 3.6 (2014), whose assembler doesn't recognize the
# AVX-512 instructions in chacha-x86_64.S.  Fall back to the C path
# (chacha_enc.c, selected by the default ?= in ../../chacha.inc).
.if !defined(__MINIX)
CHACHA_SRCS = chacha-x86_64.S
.endif

.include "../../chacha.inc"
