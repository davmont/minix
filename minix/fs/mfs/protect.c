#include "fs.h"
#include "inode.h"
#include "super.h"
#include <sys/stat.h>


/*===========================================================================*
 *				fs_chflags				     *
 *===========================================================================*/
int fs_chflags(ino_t ino_nr, int flags, int privileged)
{
/* Set a file's flags (chflags(2)).  Only the V4 wide inode has on-disk room
 * for them (d4_flags); on V1/V2/V3 and a plain V4 the operation is reported
 * as unsupported rather than silently dropped.  The super-user-only flags
 * (SF_*) may only be changed by the super-user. */
  struct inode *rip;
  u32_t newflags = (u32_t) flags;

  if ((rip = get_inode(fs_dev, ino_nr)) == NULL)
	return(EINVAL);

  if (rip->i_sp->s_rd_only) {
	put_inode(rip);
	return(EROFS);
  }

  if (rip->i_sp->s_inode_size != V4_INODE_SIZE) {
	put_inode(rip);
	return(EOPNOTSUPP);
  }

  if (!privileged && ((rip->i_flags ^ newflags) & SF_SETTABLE)) {
	put_inode(rip);
	return(EPERM);
  }

  rip->i_flags = newflags;
  rip->i_update |= CTIME;
  IN_MARKDIRTY(rip);

  put_inode(rip);
  return(OK);
}


/*===========================================================================*
 *				fs_chmod				     *
 *===========================================================================*/
int fs_chmod(ino_t ino_nr, mode_t *mode)
{
/* Perform the chmod(name, mode) system call. */
  register struct inode *rip;

  /* Temporarily open the file. */
  if( (rip = get_inode(fs_dev, ino_nr)) == NULL)
	  return(EINVAL);
 
  if(rip->i_sp->s_rd_only) {
  	put_inode(rip);
	return EROFS;
  }

  /* An immutable or append-only file's mode may not be changed (V4). */
  if(rip->i_flags & (UF_IMMUTABLE | SF_IMMUTABLE | UF_APPEND | SF_APPEND)) {
	put_inode(rip);
	return(EPERM);
  }

  /* Now make the change. Clear setgid bit if file is not in caller's grp */
  rip->i_mode = (rip->i_mode & ~ALL_MODES) | (*mode & ALL_MODES);
  rip->i_update |= CTIME;
  IN_MARKDIRTY(rip);

  /* Return full new mode to caller. */
  *mode = rip->i_mode;

  put_inode(rip);
  return(OK);
}


/*===========================================================================*
 *				fs_chown				     *
 *===========================================================================*/
int fs_chown(ino_t ino_nr, uid_t uid, gid_t gid, mode_t *mode)
{
  register struct inode *rip;

  /* Temporarily open the file. */
  if( (rip = get_inode(fs_dev, ino_nr)) == NULL)
	  return(EINVAL);

  if(rip->i_sp->s_rd_only) {
	put_inode(rip);
	return(EROFS);
  }

  /* An immutable or append-only file's ownership may not be changed (V4). */
  if(rip->i_flags & (UF_IMMUTABLE | SF_IMMUTABLE | UF_APPEND | SF_APPEND)) {
	put_inode(rip);
	return(EPERM);
  }

  rip->i_uid = uid;
  rip->i_gid = gid;
  rip->i_mode &= ~(I_SET_UID_BIT | I_SET_GID_BIT);
  rip->i_update |= CTIME;
  IN_MARKDIRTY(rip);

  /* Update caller on current mode, as it may have changed. */
  *mode = rip->i_mode;
  put_inode(rip);
  
  return(OK);
}
