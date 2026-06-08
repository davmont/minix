.PATH.S: ${.PARSEDIR}

# MINIX clang 3.6 can't assemble the AVX-512 instructions in
# poly1305-x86_64.S.  Fall back to the C path (poly1305.c in the parent
# inc).  Without POLY1305_ASM, poly1305.c provides a pure-C impl.
.if !defined(__MINIX)
POLY1305_SRCS = poly1305-x86_64.S
POLY1305_CPPFLAGS+=-DPOLY1305_ASM
.endif

.include "../../poly1305.inc"
