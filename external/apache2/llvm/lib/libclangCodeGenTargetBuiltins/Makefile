#	Reachover library for clang CodeGen/TargetBuiltins (LLVM 22 split).

LIB=	clangCodeGenTargetBuiltins

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/CodeGen/TargetBuiltins
CPPFLAGS+=	-I${CLANG_SRCDIR}/lib/CodeGen

SRCS+=	AMDGPU.cpp \
	ARM.cpp \
	DirectX.cpp \
	Hexagon.cpp \
	NVPTX.cpp \
	PPC.cpp \
	RISCV.cpp \
	SPIR.cpp \
	SystemZ.cpp \
	WebAssembly.cpp \
	X86.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
