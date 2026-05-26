#include <stdio.h>
#include <errno.h>
#include <assert.h>

#ifndef OK
#define OK 0
#endif

#include "error.c"

int main() {
  printf("Running tests for error_convert...\n");

  /* Test successful mappings from error_map */
  assert(error_convert(0) == OK);           /* no error */
  assert(error_convert(1) == ENOENT);       /* no such file/directory */
  assert(error_convert(2) == EBADF);        /* invalid handle */
  assert(error_convert(3) == EPERM);        /* operation not permitted */
  assert(error_convert(4) == EEXIST);       /* file already exists */
  assert(error_convert(5) == ENOTDIR);      /* not a directory */
  assert(error_convert(6) == ENOTEMPTY);    /* directory not empty */
  assert(error_convert(7) == EIO);          /* protocol error */
  assert(error_convert(8) == EACCES);       /* access denied */
  assert(error_convert(9) == EINVAL);       /* invalid name */
  assert(error_convert(10) == EIO);         /* generic error */
  assert(error_convert(11) == EIO);         /* sharing violation */
  assert(error_convert(12) == ENOSPC);      /* no space */
  assert(error_convert(13) == ENOSYS);      /* operation not supported */
  assert(error_convert(14) == ENAMETOOLONG);/* name too long */
  assert(error_convert(15) == EINVAL);      /* invalid parameter */

  /* Test out-of-bounds cases (should return EIO) */
  assert(error_convert(-1) == EIO);
  assert(error_convert(16) == EIO);
  assert(error_convert(100) == EIO);

  printf("test_error passed\n");
  return 0;
}
