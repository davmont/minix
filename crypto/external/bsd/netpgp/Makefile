#	$NetBSD: Makefile,v 1.10 2018/04/29 19:47:34 sevan Exp $

# Only the signature *verifier* is built.  The full netpgp OpenPGP
# implementation (libnetpgp, netpgp(1), netpgpkeys(1) and the libmj/libpaa
# support libraries) was removed: it is unmaintained upstream and nothing in
# the base system used it.  libnetpgpverify stays because pkg_install links
# against it to verify package signatures -- dropping that would lose a
# security feature, not gain one.

SUBDIR+=	lib .WAIT
SUBDIR+=	bin

.include <bsd.subdir.mk>
