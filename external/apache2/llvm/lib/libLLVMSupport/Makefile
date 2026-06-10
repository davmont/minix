#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:54 joerg Exp $

LIB=	LLVMSupport

.include <bsd.init.mk>

CPPFLAGS+=	-I${LLVM_SRCDIR}/../third-party/siphash/include
# Use the portable-C BLAKE3 path; the reachover build compiles the C sources
# but not the per-ISA .S kernels, so disable the SIMD dispatch branches.
CPPFLAGS+=	-DBLAKE3_NO_AVX512 -DBLAKE3_NO_AVX2 \
		-DBLAKE3_NO_SSE41 -DBLAKE3_NO_SSE2

# MINIX libc/libc++ does not yet provide __cxa_thread_atexit, which thread_local
# objects with destructors require (e.g. Parallel.cpp). Supply a weak stub for
# the target so the LLVM tools link; the thread_local objects simply leak.
# TODO: implement __cxa_thread_atexit(_impl) properly in MINIX libc.
SRCS+=		minix_cxa_thread_atexit.cpp
CLEANFILES+=	minix_cxa_thread_atexit.cpp
minix_cxa_thread_atexit.cpp:
	printf '#if defined(__minix)\nextern "C" int __cxa_thread_atexit(void (*)(void *), void *, void *) { return 0; }\n#endif\n' > ${.TARGET}

.PATH: ${LLVM_SRCDIR}/lib/Support
.PATH: ${LLVM_SRCDIR}/lib/Support/BLAKE3
.PATH: ${LLVM_SRCDIR}/lib/Support/LSP

SRCS+=	AArch64AttributeParser.cpp \
	AArch64BuildAttributes.cpp \
	ABIBreak.cpp \
	AMDGPUMetadata.cpp \
	APFixedPoint.cpp \
	APFloat.cpp \
	APInt.cpp \
	APSInt.cpp \
	ARMAttributeParser.cpp \
	ARMBuildAttributes.cpp \
	ARMWinEH.cpp \
	AllocToken.cpp \
	Allocator.cpp \
	Atomic.cpp \
	AutoConvert.cpp \
	BalancedPartitioning.cpp \
	Base64.cpp \
	BinaryStreamError.cpp \
	BinaryStreamReader.cpp \
	BinaryStreamRef.cpp \
	BinaryStreamWriter.cpp \
	BlockFrequency.cpp \
	BranchProbability.cpp \
	BuryPointer.cpp \
	COM.cpp \
	CRC.cpp \
	CSKYAttributeParser.cpp \
	CSKYAttributes.cpp \
	CachePruning.cpp \
	Caching.cpp \
	Chrono.cpp \
	CodeGenCoverage.cpp \
	CommandLine.cpp \
	Compression.cpp \
	ConvertEBCDIC.cpp \
	ConvertUTF.cpp \
	ConvertUTFWrapper.cpp \
	CrashRecoveryContext.cpp \
	DAGDeltaAlgorithm.cpp \
	DJB.cpp \
	DXILABI.cpp \
	DataExtractor.cpp \
	Debug.cpp \
	DebugCounter.cpp \
	DeltaAlgorithm.cpp \
	DeltaTree.cpp \
	DivisionByConstantInfo.cpp \
	DynamicAPInt.cpp \
	DynamicLibrary.cpp \
	ELFAttrParserCompact.cpp \
	ELFAttrParserExtended.cpp \
	ELFAttributes.cpp \
	Errno.cpp \
	Error.cpp \
	ErrorHandling.cpp \
	ExponentialBackoff.cpp \
	ExtensibleRTTI.cpp \
	FileCollector.cpp \
	FileOutputBuffer.cpp \
	FileUtilities.cpp \
	FloatingPointMode.cpp \
	FoldingSet.cpp \
	FormatVariadic.cpp \
	FormattedStream.cpp \
	GlobPattern.cpp \
	GraphWriter.cpp \
	Hash.cpp \
	HexagonAttributeParser.cpp \
	HexagonAttributes.cpp \
	InitLLVM.cpp \
	InstructionCost.cpp \
	IntEqClasses.cpp \
	IntegerInclusiveInterval.cpp \
	IntervalMap.cpp \
	JSON.cpp \
	Jobserver.cpp \
	KnownBits.cpp \
	KnownFPClass.cpp \
	LEB128.cpp \
	LineIterator.cpp \
	Locale.cpp \
	LockFileManager.cpp \
	Logging.cpp \
	MD5.cpp \
	MSP430AttributeParser.cpp \
	MSP430Attributes.cpp \
	ManagedStatic.cpp \
	MathExtras.cpp \
	MemAlloc.cpp \
	Memory.cpp \
	MemoryBuffer.cpp \
	MemoryBufferRef.cpp \
	ModRef.cpp \
	Mustache.cpp \
	NativeFormatting.cpp \
	OptimizedStructLayout.cpp \
	OptionStrCmp.cpp \
	Optional.cpp \
	PGOOptions.cpp \
	Parallel.cpp \
	Path.cpp \
	PluginLoader.cpp \
	PrettyStackTrace.cpp \
	Process.cpp \
	Program.cpp \
	ProgramStack.cpp \
	Protocol.cpp \
	RISCVAttributeParser.cpp \
	RISCVAttributes.cpp \
	RISCVISAUtils.cpp \
	RWMutex.cpp \
	RandomNumberGenerator.cpp \
	Regex.cpp \
	RewriteBuffer.cpp \
	RewriteRope.cpp \
	SHA1.cpp \
	SHA256.cpp \
	ScaledNumber.cpp \
	ScopedPrinter.cpp \
	Signals.cpp \
	Signposts.cpp \
	SipHash.cpp \
	SlowDynamicAPInt.cpp \
	SmallPtrSet.cpp \
	SmallVector.cpp \
	SourceMgr.cpp \
	SpecialCaseList.cpp \
	Statistic.cpp \
	StringExtras.cpp \
	StringMap.cpp \
	StringRef.cpp \
	StringSaver.cpp \
	SuffixTree.cpp \
	SuffixTreeNode.cpp \
	SystemUtils.cpp \
	TarWriter.cpp \
	TextEncoding.cpp \
	ThreadPool.cpp \
	Threading.cpp \
	TimeProfiler.cpp \
	Timer.cpp \
	ToolOutputFile.cpp \
	Transport.cpp \
	TrieRawHashMap.cpp \
	Twine.cpp \
	Unicode.cpp \
	UnicodeCaseFold.cpp \
	UnicodeNameToCodepoint.cpp \
	UnicodeNameToCodepointGenerated.cpp \
	Valgrind.cpp \
	VersionTuple.cpp \
	VirtualFileSystem.cpp \
	VirtualOutputBackend.cpp \
	VirtualOutputBackends.cpp \
	VirtualOutputConfig.cpp \
	VirtualOutputError.cpp \
	VirtualOutputFile.cpp \
	Watchdog.cpp \
	WithColor.cpp \
	YAMLParser.cpp \
	YAMLTraits.cpp \
	Z3Solver.cpp \
	blake3.c \
	blake3_dispatch.c \
	blake3_neon.c \
	blake3_portable.c \
	circular_raw_ostream.cpp \
	raw_os_ostream.cpp \
	raw_ostream.cpp \
	raw_ostream_proxy.cpp \
	raw_socket_stream.cpp \
	regcomp.c \
	regerror.c \
	regexec.c \
	regfree.c \
	regstrlcpy.c \
	xxhash.cpp \
	zOSLibFunctions.cpp




.for src in ${SRCS:M*.c}
CPPFLAGS.${src}+=	-std=gnu99
.endfor

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
