#ifndef _EXFAT_CONST_H_
#define _EXFAT_CONST_H_

/* Size of the in-core node table. */
#define NR_INODES	256

/* Synthetic inode number of the root directory (what VFS mounts). */
#define ROOT_INODE	((ino_t) 1)

/* Default permission masks for files and directories. */
#define DEFAULT_DMASK	0755
#define DEFAULT_FMASK	0755

/* Provisional sector size used to read the boot sector before the real one
 * is known.  The boot signature and all header fields live in the first 512
 * bytes regardless of the actual sector size. */
#define EXFAT_MIN_SECTOR	512

/* Accessor for an lmfs buffer's data area. */
#define b_data(bp)	((char *)(bp)->data)

#endif /* _EXFAT_CONST_H_ */
