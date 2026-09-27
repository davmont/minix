# pjdfstest: the POSIX file-system conformance suite (external/bsd/pjdfstest),
# installed under /usr/tests/pjdfstest so releasetools/qemutest.py can run it
# from the image (--suite pjdfstest).  The helper binary is built here with a
# hand-written config.h; the 238 test scripts are installed as data.

NOGCCERROR:=	yes
MKMAN=		no
WARNS?=		0

TESTSDIR=	/usr/tests/pjdfstest
PJD=		${.CURDIR}/../../../external/bsd/pjdfstest
DIST=		${PJD}/dist

.include <bsd.own.mk>

PROG=		pjdfstest
SRCS=		pjdfstest.c
.PATH:		${DIST}
CPPFLAGS+=	-I${PJD}/include
BINDIR=		${TESTSDIR}
BINOWN=		root

# The scripts, one FILES entry per file, each into its own directory.
FILESDIR=	${TESTSDIR}/tests
FILES=		${DIST}/tests/misc.sh ${DIST}/tests/conf

PJD_DIRS=	chflags chmod chown ftruncate granular link mkdir mkfifo mknod \
		open posix_fallocate rename rmdir symlink truncate unlink utimensat

.for d in ${PJD_DIRS}
_PJD_T.${d}!=	cd ${DIST}/tests/${d} && ls *.t
.for t in ${_PJD_T.${d}}
FILES+=				${DIST}/tests/${d}/${t}
FILESDIR_${DIST}/tests/${d}/${t}=	${TESTSDIR}/tests/${d}
.endfor
.endfor

.include <bsd.prog.mk>
