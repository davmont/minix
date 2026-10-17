#	$NetBSD: Makefile,v 1.5 2024/09/08 15:23:55 riastradh Exp $
#

MOZCERTS=	/usr/share/certs/mozilla
FILESDIR=	${MOZCERTS}/all

TRUSTDOMAINS=	server email code

# Normally it is a bad idea to use file system globs to list sources in
# a makefile, but we replace the directory wholesale on regen using the
# same pattern below.  So in this case, maintaining an explicit list of
# files would be more error-prone.
CERTS!=		cd ${.CURDIR:Q} && echo certs/*.pem
FILES+=		${CERTS}

.if defined(__MINIX)
# MINIX file systems (MFS) keep names of at most 60 bytes and truncate longer
# ones, which would leave these two as "....pe", which certctl(8) does not
# take for certificates: install them under shorter names.
FILESNAME_certs/Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068.pem=	Autoridad_Certificacion_Firmaprofesional_CIF_A62634068.pem
FILESNAME_certs/Hellenic_Academic_and_Research_Institutions_ECC_RootCA_2015.pem=	Hellenic_Academic_Research_Institutions_ECC_RootCA_2015.pem
.endif

.for D in ${TRUSTDOMAINS}
$D_CERTS!=	cat ${.CURDIR:Q}/$D.trust
.  for C in ${$D_CERTS}
.if defined(__MINIX)
SYMLINKS+=	../all/${C:S/^Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068$$/Autoridad_Certificacion_Firmaprofesional_CIF_A62634068/:S/^Hellenic_Academic_and_Research_Institutions_ECC_RootCA_2015$$/Hellenic_Academic_Research_Institutions_ECC_RootCA_2015/}.pem ${MOZCERTS}/$D/${C:S/^Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068$$/Autoridad_Certificacion_Firmaprofesional_CIF_A62634068/:S/^Hellenic_Academic_and_Research_Institutions_ECC_RootCA_2015$$/Hellenic_Academic_Research_Institutions_ECC_RootCA_2015/}.pem
.else
SYMLINKS+=	../all/$C.pem ${MOZCERTS}/$D/$C.pem
.endif
.  endfor
.endfor

# The upstream Mozilla certdata.txt lives in the Mozilla nss repository
# at https://hg.mozilla.org/projects/nss, under
# lib/ckfw/builtins/certdata.txt.
#
# Updates:
#
#	1. Go to:
#	   https://hg.mozilla.org/projects/nss/log/tip/lib/ckfw/builtins/certdata.txt
#
#	2. Find the top revision and follow the link to `diff'.
#
#	3. For the file lib/ckfw/builtins/certdata.txt, follow the link to
#	   `file'.
#
#	4. Follow the link to `raw'.
#
#	5. Record the date of the latest revision and the URL to the
#	   raw file in the comment below (includes hg revision).
#
#	6. Verify that the file matches when downloaded from at least
#	   three different networks.  (Suggestions: Your home
#	   residential network, a TNF server, and Tor.)
#
#	7. Once you have verified this, commit certdata.txt.
#
#	8. Review https://wiki.mozilla.org/CA/Additional_Trust_Changes
#	   for new special cases and apply to certdata.awk if
#	   appropriate.
#
#	9. After committing certdata.txt and updating certdata.awk, run
#	   `make regen' and verify that it builds and installs and
#	   generally looks sensible.
#
#	10. Once you have verified that it builds and installs, cvs add
#	   any new files and cvs rm any deleted files under certs/,
#	   and commit certs/ and *.trust.
#
# Latest revision, from 2024-08-23:
#
# https://hg.mozilla.org/projects/nss/raw-file/872bd5fefe12bc48a9c65e9ea7f189df243d835a/lib/ckfw/builtins/certdata.txt
#
regen: .PHONY
	rm -f certs/*.pem
	rm -f ${TRUSTDOMAINS:=.trust}
	mkdir tmp
	env LC_ALL=C \
	awk -f certdata.awk \
		-v CERTDIR=certs \
		-v CODETRUST=code.trust \
		-v EMAILTRUST=email.trust \
		-v OPENSSL=openssl \
		-v SERVERTRUST=server.trust \
		-v WORKDIR=tmp \
		<${.CURDIR:Q}/../dist/certdata.txt
	rm -rf tmp

.include <bsd.files.mk>
.include <bsd.inc.mk>		# XXX
.include <bsd.links.mk>
