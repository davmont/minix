/* This file contains a few general purpose utility routines.
 *
 * The entry points into this file are
 *   copy_path:	  copy a path name from a path request from userland
 *   fetch_name:  go get a path name from user space
 *   panic:       something awful has occurred;  MINIX cannot continue
 *   in_group:    determines if group 'grp' is in rfp->fp_sgroups[]
 */

#include "fs.h"
#include <minix/callnr.h>
#include <minix/endpoint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "file.h"
#include "vmnt.h"

/*===========================================================================*
 *				get_msg_path				     *
 *===========================================================================*/
static int get_msg_path(vir_bytes name, size_t len, const char *buf,
	size_t bufsize, char *dest, size_t size)
{
/* Go get the path of a path request: from the message's 'buf' if it fits
 * there (with its nul), else from the caller's address 'name'.  Put it in
 * 'dest', which should be at least PATH_MAX in size.
 */
  assert(size >= PATH_MAX);

  if (len == 0)			/* unknown: a string at 'name' */
	return fetch_name(name, 0, dest);
  if (len > size) {	/* 'len' includes terminating-nul */
	err_code = ENAMETOOLONG;
	return(EGENERIC);
  }

  /* Is the string contained in the message? If not, perform a normal copy. */
  if (len > bufsize)
	return fetch_name(name, len, dest);

  /* Just copy the path from the message */
  strncpy(dest, buf, len);

  if (dest[len - 1] != '\0') {
	err_code = ENAMETOOLONG;
	return(EGENERIC);
  }

  return(OK);
}

/*===========================================================================*
 *				copy_path				     *
 *===========================================================================*/
int copy_path(char *dest, size_t size)
{
/* Get the path of a request with a mess_lc_vfs_path message. */

  return get_msg_path(job_m_in.m_lc_vfs_path.name,
	job_m_in.m_lc_vfs_path.len, job_m_in.m_lc_vfs_path.buf,
	M_PATH_STRING_MAX, dest, size);
}

/*===========================================================================*
 *				copy_pathat				     *
 *===========================================================================*/
int copy_pathat(char *dest, size_t size)
{
/* Get the path of a request with a mess_lc_vfs_pathat message. */

  return get_msg_path(job_m_in.m_lc_vfs_pathat.name,
	job_m_in.m_lc_vfs_pathat.len, job_m_in.m_lc_vfs_pathat.buf,
	M_PATHAT_STRING_MAX, dest, size);
}

/*===========================================================================*
 *				fetch_name				     *
 *===========================================================================*/
int fetch_name(vir_bytes path, size_t len, char *dest)
{
/* Go get path and put it in 'dest'.  With 'len' 0 the length is not known:
 * copy the string a page at a time, like copyinstr(9), so that a bad address
 * fails with EFAULT and a string that ends before an unmapped page is fine.
 */
  int r;

  if (len == 0) {
	size_t off = 0, chunk, want = 64;
	char *nul;

	/* Most paths are short: copy 64 bytes first, then more and more, but
	 * never across a page boundary in one copy.
	 */
	while (off < PATH_MAX) {
		chunk = PAGE_SIZE - ((path + off) & (PAGE_SIZE - 1));
		if (chunk > want)
			chunk = want;
		if (chunk > PATH_MAX - off)
			chunk = PATH_MAX - off;
		want *= 4;
		r = sys_datacopy_wrapper(who_e, path + off, VFS_PROC_NR,
		    (vir_bytes) (dest + off), chunk);
		if (r != OK) {
			err_code = EFAULT;
			return(EGENERIC);
		}
		if ((nul = memchr(dest + off, '\0', chunk)) != NULL)
			return(OK);
		off += chunk;
	}
	err_code = ENAMETOOLONG;
	return(EGENERIC);
  }

  if (len > PATH_MAX) {	/* 'len' includes terminating-nul */
	err_code = ENAMETOOLONG;
	return(EGENERIC);
  }

  /* Check name length for validity: at least the nul. */
  if (len == 0 || len > SSIZE_MAX) {
	err_code = EINVAL;
	return(EGENERIC);
  }

  /* String is not contained in the message.  Get it from user space. */
  r = sys_datacopy_wrapper(who_e, path, VFS_PROC_NR, (vir_bytes) dest, len);
  if (r != OK) {
	err_code = EINVAL;
	return(r);
  }

  if (dest[len - 1] != '\0') {
	err_code = ENAMETOOLONG;
	return(EGENERIC);
  }

  return(OK);
}

/*===========================================================================*
 *				isokendpt_f				     *
 *===========================================================================*/
int isokendpt_f(const char *file, int line, endpoint_t endpoint, int *proc,
       int fatal)
{
  int failed = 0;
  endpoint_t ke;
  *proc = _ENDPOINT_P(endpoint);
  if (endpoint == NONE) {
	printf("VFS %s:%d: endpoint is NONE\n", file, line);
	failed = 1;
  } else if (*proc < 0 || *proc >= NR_PROCS) {
	printf("VFS %s:%d: proc (%d) from endpoint (%d) out of range\n",
		file, line, *proc, endpoint);
	failed = 1;
  } else if ((ke = fproc[*proc].fp_endpoint) != endpoint) {
	if(ke == NONE) {
		assert(fproc[*proc].fp_pid == PID_FREE);
	} else {
		printf("VFS %s:%d: proc (%d) from endpoint (%d) doesn't match "
			"known endpoint (%d)\n", file, line, *proc, endpoint,
			fproc[*proc].fp_endpoint);
		assert(fproc[*proc].fp_pid != PID_FREE);
	}
	failed = 1;
  }

  if(failed && fatal)
	panic("isokendpt_f failed");

  return(failed ? EDEADEPT : OK);
}

/*===========================================================================*
 *                              in_group                                     *
 *===========================================================================*/
int in_group(struct fproc *rfp, gid_t grp)
{
  int i;

  for (i = 0; i < rfp->fp_ngroups; i++)
	if (rfp->fp_sgroups[i] == grp)
		return(OK);

  return(EINVAL);
}

/*===========================================================================*
 *                              sys_datacopy_wrapper                         *
 *===========================================================================*/
int sys_datacopy_wrapper(endpoint_t src, vir_bytes srcv,
	endpoint_t dst, vir_bytes dstv, size_t len)
{
	/* Safe function to copy data from or to a user buffer.
	 * VFS has to be a bit more careful as a regular copy
	 * might trigger VFS action needed by VM while it's
	 * blocked on the kernel call. This wrapper tries the
	 * copy, invokes VM itself asynchronously if necessary,
	 * then tries the copy again.
	 *
	 * This function assumes it's between VFS and a user process,
	 * and therefore one of the endpoints is SELF (VFS).
	 */
	int r;
	endpoint_t them = NONE;
	vir_bytes themv = -1;
	int writable = -1;

	r = sys_datacopy_try(src, srcv, dst, dstv, len);

	if(src == VFS_PROC_NR) src = SELF;
	if(dst == VFS_PROC_NR) dst = SELF;

	assert(src == SELF || dst == SELF);

	if(src == SELF) { them = dst; themv = dstv; writable = 1; }
	if(dst == SELF) { them = src; themv = srcv; writable = 0; }

	assert(writable >= 0);
	assert(them != SELF);

	if(r == EFAULT) {
		/* The copy has failed with EFAULT, this means the kernel has
		 * given up but it might not be a legitimate error. Ask VM.
		 */
		if((r=vm_vfs_procctl_handlemem(them, themv, len, writable)) != OK) {
			return r;
		}

		r = sys_datacopy_try(src, srcv, dst, dstv, len);
	}

	return r;
}

