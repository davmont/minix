/* pjdfstest config.h for MINIX 3 amd64, written by hand for the cross-build
 * (the tree cannot run pjdfstest's configure against the target).  A HAVE_
 * line here means the symbol exists in libc; several of the *at() calls are
 * libc shims that only accept AT_FDCWD or an absolute path, and the tests
 * that pass a real directory fd are exactly the ones that should fail. */
#define HAVE_CHFLAGS 1
#define HAVE_FCHFLAGS 1
#define HAVE_LCHFLAGS 1
#define HAVE_LCHMOD 1
#define HAVE_UTIMENSAT 1
#define HAVE_FACCESSAT 1
#define HAVE_FCHMODAT 1
#define HAVE_FCHOWNAT 1
#define HAVE_FSTATAT 1
#define HAVE_LINKAT 1
#define HAVE_MKDIRAT 1
#define HAVE_MKFIFOAT 1
#define HAVE_MKNODAT 1
#define HAVE_OPENAT 1
#define HAVE_READLINKAT 1
#define HAVE_RENAMEAT 1
#define HAVE_SYMLINKAT 1
/* not in MINIX libc: posix_fallocate lpathconf bindat connectat chflagsat */
#define HAVE_SYS_ACL_H 1
#define HAVE_ACL_FROM_TEXT 1
#define HAVE_ACL_GET_ENTRY 1
#define HAVE_ACL_GET_FILE 1
#define HAVE_ACL_SET_FILE 1
#define HAVE_STRUCT_STAT_ST_ATIM 1
#define HAVE_STRUCT_STAT_ST_MTIM 1
#define HAVE_STRUCT_STAT_ST_CTIM 1
#define HAVE_STRUCT_STAT_ST_BIRTHTIM 1
