#	$NetBSD: Makefile,v 1.3 2021/05/30 01:56:53 joerg Exp $

LIB=	LLVMInstrumentation

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Transforms/Instrumentation

SRCS+=	AddressSanitizer.cpp \
	AllocToken.cpp \
	BlockCoverageInference.cpp \
	BoundsChecking.cpp \
	CGProfile.cpp \
	ControlHeightReduction.cpp \
	DataFlowSanitizer.cpp \
	GCOVProfiling.cpp \
	HWAddressSanitizer.cpp \
	IndirectCallPromotion.cpp \
	InstrProfiling.cpp \
	KCFI.cpp \
	LowerAllowCheckPass.cpp \
	MemProfInstrumentation.cpp \
	MemProfUse.cpp \
	MemorySanitizer.cpp \
	NumericalStabilitySanitizer.cpp \
	PGOCtxProfFlattening.cpp \
	PGOCtxProfLowering.cpp \
	PGOForceFunctionAttrs.cpp \
	PGOInstrumentation.cpp \
	PGOMemOPSizeOpt.cpp \
	RealtimeSanitizer.cpp \
	SanitizerBinaryMetadata.cpp \
	SanitizerCoverage.cpp \
	ThreadSanitizer.cpp \
	TypeSanitizer.cpp \
	ValueProfileCollector.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif

CWARNFLAGS.gcc+=	${GCC_NO_ADDR_OF_PACKED_MEMBER}
