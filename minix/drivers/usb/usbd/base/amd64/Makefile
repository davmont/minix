# Makefile for the AMD64 USBD

PROG=		usbd
SRCS=		usbd.c usbd_amd64.c hcd.c hcd_common.c hcd_ddekit.c hcd_schedule.c \
		ehci_pci.c ehci_mem.c ehci_core.c \
		ohci_pci.c ohci_mem.c ohci_core.c
MAN=
BINDIR=		/service

.PATH:		${.CURDIR}/../ \
		${.CURDIR}/../../hcd \
		${.CURDIR}/../../hcd/ehci \
		${.CURDIR}/../../hcd/ohci

CPPFLAGS+=	-I${.CURDIR}/../../include
CPPFLAGS+=	-I${NETBSDSRCDIR}/sys

DPADD+=		${LIBDDEKIT} ${LIBDDEKIT_USB_SERVER} ${LIBDEVMAN} ${LIBUSB} ${LIBMINC} ${LIBSYS} ${LIBPCI}
LDADD+=		-lddekit -lddekit_usb_server -ldevman -lusb -lminc -lsys -lpci

.include <minix.service.mk>
