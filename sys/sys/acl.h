/*	$NetBSD: acl.h (MINIX POSIX.1e) $	*/

/*
 * POSIX.1e access control lists for MINIX.
 *
 * ACLs are stored as extended attributes in the system namespace:
 *   system.posix_acl_access  -- the access ACL of any file
 *   system.posix_acl_default -- the default ACL of a directory
 * The on-disk value uses the Linux POSIX_ACL_XATTR binary layout (a small
 * version header followed by fixed-size, little-endian entries), so the format
 * is portable.  The userland API below follows POSIX.1e / the FreeBSD <sys/acl.h>
 * interface.
 */

#ifndef _SYS_ACL_H_
#define _SYS_ACL_H_

#include <sys/types.h>

/* Validity / size limits. */
#define ACL_MAX_ENTRIES		1024

/* acl_tag_t values: the kind of an ACL entry. */
#define ACL_USER_OBJ		0x00000001	/* owner */
#define ACL_USER		0x00000002	/* additional user; qualifier=uid */
#define ACL_GROUP_OBJ		0x00000004	/* owning group */
#define ACL_GROUP		0x00000008	/* additional group; qual=gid */
#define ACL_MASK		0x00000010	/* maximum group-class perms */
#define ACL_OTHER		0x00000020	/* everyone else */

/* acl_perm_t bits (an acl_permset_t is a set of these). */
#define ACL_EXECUTE		0x0001
#define ACL_WRITE		0x0002
#define ACL_READ		0x0004
#define ACL_PERM_BITS		(ACL_EXECUTE | ACL_WRITE | ACL_READ)

/* acl_type_t values: which ACL of a file. */
#define ACL_TYPE_ACCESS		0
#define ACL_TYPE_DEFAULT	1

/* Used as an entry qualifier when none applies, and by acl_get_entry(). */
#define ACL_UNDEFINED_ID	((id_t)-1)
#define ACL_FIRST_ENTRY		0
#define ACL_NEXT_ENTRY		1

/* acl_to_text() flags. */
#define ACL_TEXT_VERBOSE	0x01	/* (accepted, currently a no-op) */
#define ACL_TEXT_NUMERIC_IDS	0x02	/* print numeric uid/gid, not names */

typedef int		acl_tag_t;
typedef int		acl_type_t;
typedef int		acl_perm_t;
typedef unsigned int	acl_permset_t;

/* One ACL entry. */
struct acl_entry {
	acl_tag_t	ae_tag;		/* ACL_USER_OBJ, ACL_USER, ... */
	id_t		ae_id;		/* uid/gid for USER/GROUP, else undef */
	acl_perm_t	ae_perm;	/* ACL_READ | ACL_WRITE | ACL_EXECUTE */
};
typedef struct acl_entry *acl_entry_t;

/* An in-memory ACL. */
struct acl {
	unsigned int		acl_cnt;
	int			acl_next;	/* iterator for acl_get_entry() */
	struct acl_entry	acl_entry[ACL_MAX_ENTRIES];
};
typedef struct acl *acl_t;

/* The on-disk / xattr binary format (Linux POSIX_ACL_XATTR, little-endian). */
#define POSIX_ACL_XATTR_VERSION	0x0002
#define POSIX_ACL_XATTR_ACCESS	"posix_acl_access"	/* system namespace */
#define POSIX_ACL_XATTR_DEFAULT	"posix_acl_default"	/* system namespace */

struct posix_acl_xattr_header {
	uint32_t	a_version;
};
struct posix_acl_xattr_entry {
	uint16_t	e_tag;
	uint16_t	e_perm;
	uint32_t	e_id;
};

#include <sys/cdefs.h>
__BEGIN_DECLS
/* Allocation / lifetime. */
acl_t	acl_init(int _count);
int	acl_free(void *_obj);
acl_t	acl_dup(acl_t _acl);

/* Whole-ACL get/set on files. */
acl_t	acl_get_file(const char *_path, acl_type_t _type);
acl_t	acl_get_link_np(const char *_path, acl_type_t _type);
acl_t	acl_get_fd(int _fd);
acl_t	acl_get_fd_np(int _fd, acl_type_t _type);
int	acl_set_file(const char *_path, acl_type_t _type, acl_t _acl);
int	acl_set_link_np(const char *_path, acl_type_t _type, acl_t _acl);
int	acl_set_fd(int _fd, acl_t _acl);
int	acl_set_fd_np(int _fd, acl_t _acl, acl_type_t _type);
int	acl_delete_def_file(const char *_path);
int	acl_delete_fd_np(int _fd, acl_type_t _type);
int	acl_delete_file_np(const char *_path, acl_type_t _type);

/* Entry manipulation. */
int	acl_create_entry(acl_t *_acl_p, acl_entry_t *_entry_p);
int	acl_get_entry(acl_t _acl, int _entry_id, acl_entry_t *_entry_p);
int	acl_delete_entry(acl_t _acl, acl_entry_t _entry);
int	acl_get_tag_type(acl_entry_t _entry, acl_tag_t *_tag_p);
int	acl_set_tag_type(acl_entry_t _entry, acl_tag_t _tag);
void   *acl_get_qualifier(acl_entry_t _entry);
int	acl_set_qualifier(acl_entry_t _entry, const void *_qual);
int	acl_get_permset(acl_entry_t _entry, acl_permset_t *_permset_p);
int	acl_set_permset(acl_entry_t _entry, acl_permset_t _permset);
int	acl_add_perm(acl_permset_t _permset, acl_perm_t _perm);
int	acl_clear_perms(acl_permset_t _permset);
int	acl_delete_perm(acl_permset_t _permset, acl_perm_t _perm);
int	acl_get_perm_np(acl_permset_t _permset, acl_perm_t _perm);

/* Whole-ACL helpers. */
int	acl_valid(acl_t _acl);
int	acl_calc_mask(acl_t *_acl_p);

/* Text <-> ACL. */
acl_t	acl_from_text(const char *_buf);
char   *acl_to_text(acl_t _acl, ssize_t *_len_p);

/* Binary (xattr) <-> ACL, exposed for tools and the kernel-side evaluator. */
ssize_t	acl_copy_ext(void *_buf, acl_t _acl, ssize_t _size);
ssize_t	acl_size(acl_t _acl);
__END_DECLS

#endif /* !_SYS_ACL_H_ */
