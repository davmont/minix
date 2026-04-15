.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	usbd
.endif # ${MACHINE_ARCH} == "earm"

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR+=	usbd
.endif # ${MACHINE_ARCH} == "x86_64"

SUBDIR+=	usb_storage usb_hub

.include <bsd.subdir.mk>
