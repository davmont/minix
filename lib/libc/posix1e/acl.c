/*
 * POSIX.1e access control lists for MINIX.
 *
 * ACLs live in the system-namespace extended attributes posix_acl_access and
 * posix_acl_default, stored in the portable Linux POSIX_ACL_XATTR binary form.
 * This file provides the POSIX.1e userland API (acl_*), translating between the
 * in-memory acl_t, that binary form, and the textual representation used by
 * getfacl(1)/setfacl(1).
 */
#include <sys/types.h>
#include <sys/acl.h>
#include <sys/extattr.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* allocation and lifetime						      */
/* ------------------------------------------------------------------ */

acl_t
acl_init(int count)
{
	acl_t acl;

	if (count < 0 || count > ACL_MAX_ENTRIES) { errno = EINVAL; return NULL; }
	if ((acl = calloc(1, sizeof(*acl))) == NULL) return NULL;
	acl->acl_cnt = 0;
	acl->acl_next = 0;
	return acl;
}

int
acl_free(void *obj)
{
	free(obj);
	return 0;
}

acl_t
acl_dup(acl_t acl)
{
	acl_t n;

	if (acl == NULL) { errno = EINVAL; return NULL; }
	if ((n = malloc(sizeof(*n))) == NULL) return NULL;
	memcpy(n, acl, sizeof(*n));
	return n;
}

/* ------------------------------------------------------------------ */
/* entry manipulation						      */
/* ------------------------------------------------------------------ */

int
acl_create_entry(acl_t *acl_p, acl_entry_t *entry_p)
{
	acl_t acl;

	if (acl_p == NULL || (acl = *acl_p) == NULL) { errno = EINVAL; return -1; }
	if (acl->acl_cnt >= ACL_MAX_ENTRIES) { errno = ENOSPC; return -1; }
	*entry_p = &acl->acl_entry[acl->acl_cnt];
	memset(*entry_p, 0, sizeof(struct acl_entry));
	(*entry_p)->ae_id = ACL_UNDEFINED_ID;
	acl->acl_cnt++;
	return 0;
}

int
acl_get_entry(acl_t acl, int entry_id, acl_entry_t *entry_p)
{
	if (acl == NULL) { errno = EINVAL; return -1; }
	if (entry_id == ACL_FIRST_ENTRY) acl->acl_next = 0;
	else if (entry_id != ACL_NEXT_ENTRY) { errno = EINVAL; return -1; }
	if ((unsigned) acl->acl_next >= acl->acl_cnt) return 0;	/* no more */
	*entry_p = &acl->acl_entry[acl->acl_next++];
	return 1;
}

int
acl_delete_entry(acl_t acl, acl_entry_t entry)
{
	unsigned i;

	if (acl == NULL || entry == NULL) { errno = EINVAL; return -1; }
	i = (unsigned) (entry - &acl->acl_entry[0]);
	if (i >= acl->acl_cnt) { errno = EINVAL; return -1; }
	memmove(&acl->acl_entry[i], &acl->acl_entry[i + 1],
	    (acl->acl_cnt - i - 1) * sizeof(struct acl_entry));
	acl->acl_cnt--;
	return 0;
}

int
acl_get_tag_type(acl_entry_t e, acl_tag_t *tag_p)
{
	if (e == NULL) { errno = EINVAL; return -1; }
	*tag_p = e->ae_tag;
	return 0;
}

int
acl_set_tag_type(acl_entry_t e, acl_tag_t tag)
{
	if (e == NULL) { errno = EINVAL; return -1; }
	switch (tag) {
	case ACL_USER_OBJ: case ACL_USER: case ACL_GROUP_OBJ:
	case ACL_GROUP: case ACL_MASK: case ACL_OTHER:
		e->ae_tag = tag;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

void *
acl_get_qualifier(acl_entry_t e)
{
	id_t *idp;

	if (e == NULL) { errno = EINVAL; return NULL; }
	if (e->ae_tag != ACL_USER && e->ae_tag != ACL_GROUP) {
		errno = EINVAL; return NULL;
	}
	if ((idp = malloc(sizeof(*idp))) == NULL) return NULL;
	*idp = e->ae_id;
	return idp;
}

int
acl_set_qualifier(acl_entry_t e, const void *qual)
{
	if (e == NULL || qual == NULL) { errno = EINVAL; return -1; }
	e->ae_id = *(const id_t *) qual;
	return 0;
}

int
acl_get_permset(acl_entry_t e, acl_permset_t *p)
{
	if (e == NULL) { errno = EINVAL; return -1; }
	*p = (acl_permset_t) e->ae_perm;
	return 0;
}

int
acl_set_permset(acl_entry_t e, acl_permset_t p)
{
	if (e == NULL) { errno = EINVAL; return -1; }
	e->ae_perm = (acl_perm_t) (p & ACL_PERM_BITS);
	return 0;
}

int acl_add_perm(acl_permset_t p, acl_perm_t perm) { return 0; }
int acl_clear_perms(acl_permset_t p) { return 0; }
int acl_delete_perm(acl_permset_t p, acl_perm_t perm) { return 0; }
int acl_get_perm_np(acl_permset_t p, acl_perm_t perm) { return (p & perm) != 0; }

/* The permset-by-value functions above cannot mutate the caller's set, so the
 * tools manipulate ae_perm directly; these exist for source compatibility. */

/* ------------------------------------------------------------------ */
/* validation and the mask						      */
/* ------------------------------------------------------------------ */

int
acl_valid(acl_t acl)
{
	unsigned i;
	int nuobj = 0, ngobj = 0, nother = 0, nmask = 0, nnamed = 0;

	if (acl == NULL) { errno = EINVAL; return -1; }
	if (acl->acl_cnt == 0) return 0;	/* an empty ACL is valid (no ACL) */
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:	nuobj++;  break;
		case ACL_GROUP_OBJ:	ngobj++;  break;
		case ACL_OTHER:		nother++; break;
		case ACL_MASK:		nmask++;  break;
		case ACL_USER: case ACL_GROUP:	nnamed++; break;
		default:		errno = EINVAL; return -1;
		}
	}
	if (nuobj != 1 || ngobj != 1 || nother != 1 || nmask > 1) {
		errno = EINVAL; return -1;
	}
	if (nnamed > 0 && nmask != 1) { errno = EINVAL; return -1; }
	return 0;
}

int
acl_calc_mask(acl_t *acl_p)
{
	acl_t acl;
	acl_entry_t mask = NULL;
	acl_perm_t perms = 0;
	unsigned i;

	if (acl_p == NULL || (acl = *acl_p) == NULL) { errno = EINVAL; return -1; }
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER: case ACL_GROUP: case ACL_GROUP_OBJ:
			perms |= acl->acl_entry[i].ae_perm;
			break;
		case ACL_MASK:
			mask = &acl->acl_entry[i];
			break;
		}
	}
	if (mask == NULL) {
		if (acl->acl_cnt >= ACL_MAX_ENTRIES) { errno = ENOSPC; return -1; }
		mask = &acl->acl_entry[acl->acl_cnt++];
		mask->ae_tag = ACL_MASK;
		mask->ae_id = ACL_UNDEFINED_ID;
	}
	mask->ae_perm = perms;
	return 0;
}

/* ------------------------------------------------------------------ */
/* binary (xattr) conversion					      */
/* ------------------------------------------------------------------ */

ssize_t
acl_size(acl_t acl)
{
	if (acl == NULL) { errno = EINVAL; return -1; }
	return (ssize_t) (sizeof(struct posix_acl_xattr_header) +
	    acl->acl_cnt * sizeof(struct posix_acl_xattr_entry));
}

ssize_t
acl_copy_ext(void *buf, acl_t acl, ssize_t size)
{
	struct posix_acl_xattr_header *h = buf;
	struct posix_acl_xattr_entry *e;
	unsigned i;
	ssize_t need;

	if (acl == NULL || buf == NULL) { errno = EINVAL; return -1; }
	need = acl_size(acl);
	if (size < need) { errno = ERANGE; return -1; }
	h->a_version = POSIX_ACL_XATTR_VERSION;
	e = (struct posix_acl_xattr_entry *) (h + 1);
	for (i = 0; i < acl->acl_cnt; i++, e++) {
		e->e_tag = (uint16_t) acl->acl_entry[i].ae_tag;
		e->e_perm = (uint16_t) acl->acl_entry[i].ae_perm;
		if (acl->acl_entry[i].ae_tag == ACL_USER ||
		    acl->acl_entry[i].ae_tag == ACL_GROUP)
			e->e_id = (uint32_t) acl->acl_entry[i].ae_id;
		else
			e->e_id = (uint32_t) ACL_UNDEFINED_ID;
	}
	return need;
}

/* Build an in-memory ACL from a POSIX_ACL_XATTR binary buffer of 'len' bytes. */
static acl_t
acl_copy_int_n(const void *buf, size_t len)
{
	const struct posix_acl_xattr_header *h = buf;
	const struct posix_acl_xattr_entry *e;
	acl_t acl;
	size_t n, i;

	if (buf == NULL || len < sizeof(*h) ||
	    h->a_version != POSIX_ACL_XATTR_VERSION) { errno = EINVAL; return NULL; }
	n = (len - sizeof(*h)) / sizeof(struct posix_acl_xattr_entry);
	if (n > ACL_MAX_ENTRIES) { errno = EINVAL; return NULL; }
	if ((acl = acl_init(0)) == NULL) return NULL;
	e = (const struct posix_acl_xattr_entry *) (h + 1);
	for (i = 0; i < n; i++, e++) {
		acl->acl_entry[i].ae_tag = (acl_tag_t) e->e_tag;
		acl->acl_entry[i].ae_perm = (acl_perm_t) (e->e_perm & ACL_PERM_BITS);
		acl->acl_entry[i].ae_id = (id_t) e->e_id;
	}
	acl->acl_cnt = (unsigned) n;
	return acl;
}

/* ------------------------------------------------------------------ */
/* file get/set, via the extended-attribute interface		      */
/* ------------------------------------------------------------------ */

static const char *
acl_xattr_name(acl_type_t type)
{
	switch (type) {
	case ACL_TYPE_ACCESS:	return POSIX_ACL_XATTR_ACCESS;
	case ACL_TYPE_DEFAULT:	return POSIX_ACL_XATTR_DEFAULT;
	}
	return NULL;
}

static acl_t
acl_get_common(const char *path, int fd, acl_type_t type, int link)
{
	char buf[sizeof(struct posix_acl_xattr_header) +
	    ACL_MAX_ENTRIES * sizeof(struct posix_acl_xattr_entry)];
	const char *name = acl_xattr_name(type);
	ssize_t r;

	if (name == NULL) { errno = EINVAL; return NULL; }
	if (path != NULL)
		r = link ? extattr_get_link(path, EXTATTR_NAMESPACE_SYSTEM, name,
			       buf, sizeof(buf))
			 : extattr_get_file(path, EXTATTR_NAMESPACE_SYSTEM, name,
			       buf, sizeof(buf));
	else
		r = extattr_get_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name, buf,
		    sizeof(buf));
	if (r < 0) {
		/* No ACL present: report an empty ACL, as POSIX.1e expects. */
		if (errno == ENOATTR) return acl_init(0);
		return NULL;
	}
	return acl_copy_int_n(buf, (size_t) r);
}

acl_t acl_get_file(const char *p, acl_type_t t){return acl_get_common(p,-1,t,0);}
acl_t acl_get_link_np(const char *p, acl_type_t t){return acl_get_common(p,-1,t,1);}
acl_t acl_get_fd_np(int fd, acl_type_t t){return acl_get_common(NULL,fd,t,0);}
acl_t acl_get_fd(int fd){return acl_get_common(NULL,fd,ACL_TYPE_ACCESS,0);}

/* Derive the file mode's rwxrwxrwx bits from an access ACL and apply them, so
 * the mode and the ACL stay consistent (POSIX.1e requirement). */
static int
acl_sync_mode(const char *path, int fd, acl_t acl)
{
	struct stat st;
	mode_t m;
	unsigned i;
	int have_mask = 0;
	acl_perm_t uobj = 0, gobj = 0, other = 0, mask = 0;

	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:  uobj  = acl->acl_entry[i].ae_perm; break;
		case ACL_GROUP_OBJ: gobj  = acl->acl_entry[i].ae_perm; break;
		case ACL_OTHER:     other = acl->acl_entry[i].ae_perm; break;
		case ACL_MASK: mask = acl->acl_entry[i].ae_perm; have_mask=1; break;
		}
	}
	if ((path ? stat(path, &st) : fstat(fd, &st)) != 0) return -1;
	m = st.st_mode & ~(mode_t)0777;
	m |= (uobj & 7) << 6;
	m |= ((have_mask ? mask : gobj) & 7) << 3;	/* group class = mask */
	m |= (other & 7);
	return path ? chmod(path, m) : fchmod(fd, m);
}

static int
acl_set_common(const char *path, int fd, acl_type_t type, acl_t acl)
{
	char buf[sizeof(struct posix_acl_xattr_header) +
	    ACL_MAX_ENTRIES * sizeof(struct posix_acl_xattr_entry)];
	const char *name = acl_xattr_name(type);
	ssize_t n;

	if (name == NULL || acl == NULL) { errno = EINVAL; return -1; }
	if (acl_valid(acl) != 0) return -1;
	if ((n = acl_copy_ext(buf, acl, (ssize_t) sizeof(buf))) < 0) return -1;
	if (path != NULL) {
		if (extattr_set_file(path, EXTATTR_NAMESPACE_SYSTEM, name, buf,
		    (size_t) n) != 0) return -1;
	} else {
		if (extattr_set_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name, buf,
		    (size_t) n) != 0) return -1;
	}
	if (type == ACL_TYPE_ACCESS)
		return acl_sync_mode(path, fd, acl);
	return 0;
}

int acl_set_file(const char *p, acl_type_t t, acl_t a){return acl_set_common(p,-1,t,a);}
int acl_set_fd_np(int fd, acl_t a, acl_type_t t){return acl_set_common(NULL,fd,t,a);}
int acl_set_fd(int fd, acl_t a){return acl_set_common(NULL,fd,ACL_TYPE_ACCESS,a);}
int acl_set_link_np(const char *p, acl_type_t t, acl_t a){return acl_set_common(p,-1,t,a);}

int
acl_delete_def_file(const char *path)
{
	if (extattr_delete_file(path, EXTATTR_NAMESPACE_SYSTEM,
	    POSIX_ACL_XATTR_DEFAULT) != 0 && errno != ENOATTR)
		return -1;
	return 0;
}

int
acl_delete_file_np(const char *path, acl_type_t type)
{
	const char *name = acl_xattr_name(type);
	if (name == NULL) { errno = EINVAL; return -1; }
	if (extattr_delete_file(path, EXTATTR_NAMESPACE_SYSTEM, name) != 0 &&
	    errno != ENOATTR) return -1;
	return 0;
}

int
acl_delete_fd_np(int fd, acl_type_t type)
{
	const char *name = acl_xattr_name(type);
	if (name == NULL) { errno = EINVAL; return -1; }
	if (extattr_delete_fd(fd, EXTATTR_NAMESPACE_SYSTEM, name) != 0 &&
	    errno != ENOATTR) return -1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* text representation						      */
/* ------------------------------------------------------------------ */

static acl_perm_t
parse_perms(const char *s)
{
	acl_perm_t p = 0;
	for (; *s != '\0'; s++) {
		switch (*s) {
		case 'r': p |= ACL_READ;    break;
		case 'w': p |= ACL_WRITE;   break;
		case 'x': p |= ACL_EXECUTE; break;
		case '-': case ' ': break;
		default: return (acl_perm_t) -1;
		}
	}
	return p;
}

acl_t
acl_from_text(const char *buf)
{
	acl_t acl;
	char *dup, *line, *saveline;

	if (buf == NULL) { errno = EINVAL; return NULL; }
	if ((acl = acl_init(0)) == NULL) return NULL;
	if ((dup = strdup(buf)) == NULL) { acl_free(acl); return NULL; }

	for (line = strtok_r(dup, "\n,", &saveline); line != NULL;
	     line = strtok_r(NULL, "\n,", &saveline)) {
		char *f[3], *p, *c;
		int nf = 0;
		acl_entry_t e;
		acl_tag_t tag;
		acl_perm_t perm;
		id_t id = ACL_UNDEFINED_ID;

		while (isspace((unsigned char) *line)) line++;
		if (*line == '\0' || *line == '#') continue;
		/* strip an optional "default:" prefix */
		if (strncmp(line, "default:", 8) == 0) line += 8;
		else if (strncmp(line, "d:", 2) == 0) line += 2;

		for (p = line; nf < 3; ) {
			f[nf++] = p;
			if ((c = strchr(p, ':')) == NULL) break;
			*c = '\0';
			p = c + 1;
		}
		/* The fields are tag[:qualifier[:perms]].  The qualifier is
		 * always field 1 (possibly empty) and the perms field 2; perms
		 * may be absent (as in a setfacl -x 'tag:qualifier' spec). */
		if (nf < 2) f[1] = "";

		if (!strcmp(f[0], "u") || !strcmp(f[0], "user"))
			tag = (f[1][0] != '\0') ? ACL_USER : ACL_USER_OBJ;
		else if (!strcmp(f[0], "g") || !strcmp(f[0], "group"))
			tag = (f[1][0] != '\0') ? ACL_GROUP : ACL_GROUP_OBJ;
		else if (!strcmp(f[0], "m") || !strcmp(f[0], "mask"))
			tag = ACL_MASK;
		else if (!strcmp(f[0], "o") || !strcmp(f[0], "other"))
			tag = ACL_OTHER;
		else goto bad;

		if (tag == ACL_USER || tag == ACL_GROUP) {
			char *end;
			long v = strtol(f[1], &end, 10);
			if (*end == '\0') id = (id_t) v;
			else if (tag == ACL_USER) {
				struct passwd *pw = getpwnam(f[1]);
				if (pw == NULL) goto bad;
				id = pw->pw_uid;
			} else {
				struct group *gr = getgrnam(f[1]);
				if (gr == NULL) goto bad;
				id = gr->gr_gid;
			}
		}
		perm = (nf >= 3) ? parse_perms(f[2]) : 0;
		if (perm == (acl_perm_t) -1) goto bad;

		if (acl_create_entry(&acl, &e) != 0) goto fail;
		e->ae_tag = tag;
		e->ae_id = id;
		e->ae_perm = perm;
	}
	free(dup);
	return acl;
bad:
	errno = EINVAL;
fail:
	free(dup);
	acl_free(acl);
	return NULL;
}

static void
fmt_perms(char *out, acl_perm_t p)
{
	out[0] = (p & ACL_READ)    ? 'r' : '-';
	out[1] = (p & ACL_WRITE)   ? 'w' : '-';
	out[2] = (p & ACL_EXECUTE) ? 'x' : '-';
	out[3] = '\0';
}

char *
acl_to_text(acl_t acl, ssize_t *len_p)
{
	char *out, *q;
	size_t cap;
	unsigned i;

	if (acl == NULL) { errno = EINVAL; return NULL; }
	cap = acl->acl_cnt * (32 + 16) + 1;	/* generous per entry */
	if ((out = malloc(cap)) == NULL) return NULL;
	q = out;
	for (i = 0; i < acl->acl_cnt; i++) {
		acl_entry_t e = &acl->acl_entry[i];
		char perms[4], name[32];
		fmt_perms(perms, e->ae_perm);
		name[0] = '\0';
		switch (e->ae_tag) {
		case ACL_USER_OBJ:
			q += sprintf(q, "user::%s\n", perms); break;
		case ACL_USER: {
			struct passwd *pw = getpwuid((uid_t) e->ae_id);
			if (pw) snprintf(name, sizeof(name), "%s", pw->pw_name);
			else snprintf(name, sizeof(name), "%lu",
			    (unsigned long) e->ae_id);
			q += sprintf(q, "user:%s:%s\n", name, perms); break; }
		case ACL_GROUP_OBJ:
			q += sprintf(q, "group::%s\n", perms); break;
		case ACL_GROUP: {
			struct group *gr = getgrgid((gid_t) e->ae_id);
			if (gr) snprintf(name, sizeof(name), "%s", gr->gr_name);
			else snprintf(name, sizeof(name), "%lu",
			    (unsigned long) e->ae_id);
			q += sprintf(q, "group:%s:%s\n", name, perms); break; }
		case ACL_MASK:
			q += sprintf(q, "mask::%s\n", perms); break;
		case ACL_OTHER:
			q += sprintf(q, "other::%s\n", perms); break;
		}
	}
	*q = '\0';
	if (len_p != NULL) *len_p = (ssize_t) (q - out);
	return out;
}
