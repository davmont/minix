#	$NetBSD: Makefile,v 1.54 2015/09/28 17:12:46 christos Exp $

.include <bsd.own.mk>

#MINIX: BIND 9.10 retired in favor of BIND 9.18 at external/mpl/bind.
SUBDIR=	byacc dejavu dhcpcd \
	fetch file flex less \
	libarchive libevent libpcap mdocml \
	openresolv tcpdump tmux top

.if (${MKATF} != "no")
SUBDIR+= atf
.endif
.if (${MKCRYPTO} != "no")
SUBDIR+= pkg_install
.endif
# IP Filter
.if (${MKIPFILTER} != "no")
SUBDIR+=ipf
.endif
.if (${MKISCSI} != "no")
SUBDIR+= iscsi 
.endif
.if (${MKKYUA} != "no")
SUBDIR+= lutok kyua-testers kyua-cli kyua-atf-compat
.endif
.if (${MKLDAP} != "no")
SUBDIR+= openldap
.endif
.if (${MKLIBCXX} != "no")
SUBDIR+= libc++
.endif
# LLVM moved to external/apache2/llvm (LLVM 13, Apache-2.0 relicense).
.if (${MKPCC} != "no")
SUBDIR+= pcc
.endif
.if (${MKDTRACE} != "no")
SUBDIR+= libproc
SUBDIR+= librtld_db
.endif

#MINIX: ISC DHCP retired -- it is EOL upstream (superseded by Kea) and uses the
# pre-9.18 libisc socket/timer/hash APIs that BIND 9.18 removed in its libuv
# netmgr rewrite, so it cannot link against the system BIND.  The modern dhcpcd
# (external/bsd/dhcpcd, the rc default) provides DHCP client functionality.
SUBDIR+= blacklist nvi

.include <bsd.subdir.mk>
