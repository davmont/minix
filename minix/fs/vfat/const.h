#ifndef _VFAT_CONST_H_
#define _VFAT_CONST_H_

/* Size of the in-core node table. */
#define NR_INODES	256

/* Synthetic inode number of the root directory (what VFS mounts). */
#define ROOT_INODE	((ino_t) 1)

/* A directory entry is 32 bytes. */
#define DIR_ENTRY_SIZE	((unsigned) sizeof(struct direntry))

/* Default permission masks when none are supplied as mount options. */
#define DEFAULT_DMASK	0755
#define DEFAULT_FMASK	0755

/* Accessor for an lmfs buffer's data area. */
#define b_data(bp)	((char *)(bp)->data)

#endif /* _VFAT_CONST_H_ */
