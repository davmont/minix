.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	fb
SUBDIR+=	tda19988
.endif # ${MACHINE_ARCH} == "earm"

# MINIX: UEFI GOP framebuffer backend (display roadmap step 1).
.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=	fb
.endif # ${MACHINE_ARCH} == "x86_64"

.include <bsd.subdir.mk>
