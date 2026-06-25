/* setfacl -- set the POSIX.1e access control list of files.
 *
 * Supported:  -m <spec>  modify/add entries     -s <spec>  set the whole ACL
 *             -x <spec>  remove entries         -b         remove extended ACL
 *             -k         remove the default ACL -d         operate on default
 * <spec> is a comma-separated list of acl_from_text(3) entries.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static acl_t
mode_acl(mode_t m)
{
	acl_t a = acl_init(3);
	acl_entry_t e;
	if (a == NULL) err(1, "acl_init");
	acl_create_entry(&a, &e); e->ae_tag = ACL_USER_OBJ;  e->ae_perm = (m>>6)&7;
	acl_create_entry(&a, &e); e->ae_tag = ACL_GROUP_OBJ; e->ae_perm = (m>>3)&7;
	acl_create_entry(&a, &e); e->ae_tag = ACL_OTHER;     e->ae_perm = m&7;
	return a;
}

static int
same_who(acl_entry_t a, acl_entry_t b)
{
	if (a->ae_tag != b->ae_tag) return 0;
	if (a->ae_tag == ACL_USER || a->ae_tag == ACL_GROUP)
		return a->ae_id == b->ae_id;
	return 1;
}

/* Merge every entry of 'src' into 'dst' (replace same-who, else append). */
static void
merge(acl_t dst, acl_t src)
{
	unsigned i, j;
	for (i = 0; i < src->acl_cnt; i++) {
		for (j = 0; j < dst->acl_cnt; j++)
			if (same_who(&dst->acl_entry[j], &src->acl_entry[i])) {
				dst->acl_entry[j].ae_perm = src->acl_entry[i].ae_perm;
				break;
			}
		if (j == dst->acl_cnt) {
			acl_entry_t e;
			if (acl_create_entry(&dst, &e) != 0) err(1, "acl_create_entry");
			*e = src->acl_entry[i];
		}
	}
}

/* Remove from 'dst' every entry whose who matches an entry of 'src'. */
static void
subtract(acl_t dst, acl_t src)
{
	unsigned i, j;
	for (i = 0; i < src->acl_cnt; i++)
		for (j = 0; j < dst->acl_cnt; j++)
			if (same_who(&dst->acl_entry[j], &src->acl_entry[i])) {
				acl_delete_entry(dst, &dst->acl_entry[j]);
				break;
			}
}

static int
has_named(acl_t a)
{
	unsigned i;
	for (i = 0; i < a->acl_cnt; i++)
		if (a->acl_entry[i].ae_tag == ACL_USER ||
		    a->acl_entry[i].ae_tag == ACL_GROUP)
			return 1;
	return 0;
}

static void
remove_mask(acl_t a)
{
	unsigned i;
	for (i = 0; i < a->acl_cnt; i++)
		if (a->acl_entry[i].ae_tag == ACL_MASK) {
			acl_delete_entry(a, &a->acl_entry[i]);
			return;
		}
}

/* Drop the named-user/named-group/mask entries, keeping the base three. */
static void
strip_extended(acl_t a)
{
	unsigned j = 0;
	acl_t out = acl_init(0);
	if (out == NULL) err(1, "acl_init");
	for (j = 0; j < a->acl_cnt; j++) {
		int t = a->acl_entry[j].ae_tag;
		if (t == ACL_USER_OBJ || t == ACL_GROUP_OBJ || t == ACL_OTHER) {
			acl_entry_t e;
			acl_create_entry(&out, &e);
			*e = a->acl_entry[j];
		}
	}
	*a = *out;
	acl_free(out);
}

int
main(int argc, char **argv)
{
	int c, dflag = 0, bflag = 0, kflag = 0;
	char op = 0;			/* 'm', 'x', or 's' */
	char *spec = NULL;
	int rc = 0;

	while ((c = getopt(argc, argv, "m:x:s:bkd")) != -1) {
		switch (c) {
		case 'm': case 'x': case 's': op = c; spec = optarg; break;
		case 'b': bflag = 1; break;
		case 'k': kflag = 1; break;
		case 'd': dflag = 1; break;
		default: goto usage;
		}
	}
	argc -= optind; argv += optind;
	if (argc == 0 || (op == 0 && !bflag && !kflag)) goto usage;

	acl_type_t type = dflag ? ACL_TYPE_DEFAULT : ACL_TYPE_ACCESS;

	while (argc-- > 0) {
		const char *path = *argv++;
		struct stat st;
		acl_t acl, sp = NULL;

		if (kflag) {		/* remove the default ACL */
			if (acl_delete_def_file(path) != 0) {
				warn("%s", path); rc = 1;
			}
			if (op == 0 && !bflag) continue;
		}
		if (stat(path, &st) != 0) { warn("%s", path); rc = 1; continue; }

		/* Load the current ACL we will modify. */
		if (op == 's') {
			acl = acl_init(0);
		} else if (type == ACL_TYPE_ACCESS) {
			acl = acl_get_file(path, ACL_TYPE_ACCESS);
			if (acl != NULL && acl->acl_cnt == 0) {
				acl_free(acl); acl = mode_acl(st.st_mode);
			}
		} else {	/* default ACL */
			acl = acl_get_file(path, ACL_TYPE_DEFAULT);
			/* Adding a default entry requires the base entries; seed
			 * them from the directory's mode when none exist yet. */
			if (acl != NULL && acl->acl_cnt == 0 &&
			    (op == 'm' || op == 's')) {
				acl_free(acl); acl = mode_acl(st.st_mode);
			}
		}
		if (acl == NULL) { warn("%s", path); rc = 1; continue; }

		if (op != 0) {
			if ((sp = acl_from_text(spec)) == NULL) {
				warnx("invalid ACL spec: %s", spec);
				acl_free(acl); rc = 1; continue;
			}
		}
		switch (op) {
		case 'm': merge(acl, sp); break;
		case 's': merge(acl, sp); break;	/* into the empty ACL */
		case 'x': subtract(acl, sp); break;
		}
		if (bflag) strip_extended(acl);

		/* A minimal ACL (no named user/group entries) carries no mask. */
		if (has_named(acl)) {
			if (acl_calc_mask(&acl) != 0) { warn("acl_calc_mask"); rc=1; }
		} else
			remove_mask(acl);
		if (acl_set_file(path, type, acl) != 0) {
			warn("%s: acl_set_file", path); rc = 1;
		}
		if (bflag && S_ISDIR(st.st_mode))
			(void) acl_delete_def_file(path);
		if (sp) acl_free(sp);
		acl_free(acl);
	}
	return rc;
usage:
	fprintf(stderr, "usage: setfacl [-bkd] [-m spec] [-x spec] "
	    "[-s spec] file ...\n");
	return 2;
}
