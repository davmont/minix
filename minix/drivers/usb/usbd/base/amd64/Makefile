# Makefile for the AMD64 USBD

PROG=		usbd
SRCS=		usbd.c usbd_amd64.c hcd.c hcd_common.c hcd_ddekit.c hcd_schedule.c \
		ehci_pci.c ehci_mem.c ehci_core.c \
		ohci_pci.c ohci_mem.c ohci_core.c \
		xhci_pci.c xhci_mem.c xhci_core.c
MAN=
BINDIR=		/service

# Install the privilege/IPC config to /etc/system.conf.d so minix-service
# picks it up when usbd is brought up.
FILES=		${.CURDIR}/../../usbd.conf
FILESNAME=	usbd
FILESDIR=	/etc/system.conf.d

.PATH:		${.CURDIR}/../ \
		${.CURDIR}/../../hcd \
		${.CURDIR}/../../hcd/ehci \
		${.CURDIR}/../../hcd/ohci \
		${.CURDIR}/../../hcd/xhci

CPPFLAGS+=	-I${.CURDIR}/../../include
CPPFLAGS+=	-I${NETBSDSRCDIR}/sys

DPADD+=		${LIBDDEKIT} ${LIBDDEKIT_USB_SERVER} ${LIBDEVMAN} ${LIBUSB} ${LIBMINC} ${LIBSYS} ${LIBPCI}
LDADD+=		-lddekit -lddekit_usb_server -ldevman -lusb -lminc -lsys -lpci

.include <minix.service.mk>
