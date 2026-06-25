/* getfacl -- display the POSIX.1e access control list of files. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int aflag, dflag;	/* -a: access only, -d: default only */

/* Build the trivial three-entry ACL implied by a file's mode bits. */
static acl_t
mode_acl(mode_t m)
{
	acl_t a = acl_init(3);
	acl_entry_t e;

	if (a == NULL) return NULL;
	acl_create_entry(&a, &e); e->ae_tag = ACL_USER_OBJ;  e->ae_perm = (m>>6)&7;
	acl_create_entry(&a, &e); e->ae_tag = ACL_GROUP_OBJ; e->ae_perm = (m>>3)&7;
	acl_create_entry(&a, &e); e->ae_tag = ACL_OTHER;     e->ae_perm = m&7;
	return a;
}

static void
print_prefixed(const char *txt, const char *prefix)
{
	const char *p = txt, *nl;
	while (*p != '\0') {
		nl = strchr(p, '\n');
		if (nl == NULL) { printf("%s%s\n", prefix, p); break; }
		printf("%s%.*s\n", prefix, (int)(nl - p), p);
		p = nl + 1;
	}
}

static int
show(const char *path)
{
	struct stat st;
	struct passwd *pw;
	struct group *gr;
	acl_t a;
	char *txt;

	if (stat(path, &st) != 0) { warn("%s", path); return 1; }

	printf("# file: %s\n", path);
	pw = getpwuid(st.st_uid);
	if (pw) printf("# owner: %s\n", pw->pw_name);
	else printf("# owner: %lu\n", (unsigned long) st.st_uid);
	gr = getgrgid(st.st_gid);
	if (gr) printf("# group: %s\n", gr->gr_name);
	else printf("# group: %lu\n", (unsigned long) st.st_gid);

	if (!dflag) {
		if ((a = acl_get_file(path, ACL_TYPE_ACCESS)) == NULL) {
			warn("%s: acl_get_file", path); return 1;
		}
		if (a->acl_cnt == 0) { acl_free(a); a = mode_acl(st.st_mode); }
		if ((txt = acl_to_text(a, NULL)) != NULL) {
			fputs(txt, stdout); free(txt);
		}
		acl_free(a);
	}
	if (!aflag && S_ISDIR(st.st_mode)) {
		if ((a = acl_get_file(path, ACL_TYPE_DEFAULT)) != NULL) {
			if (a->acl_cnt > 0 &&
			    (txt = acl_to_text(a, NULL)) != NULL) {
				print_prefixed(txt, "default:");
				free(txt);
			}
			acl_free(a);
		}
	}
	printf("\n");
	return 0;
}

int
main(int argc, char **argv)
{
	int c, rc = 0;

	while ((c = getopt(argc, argv, "ad")) != -1) {
		switch (c) {
		case 'a': aflag = 1; break;
		case 'd': dflag = 1; break;
		default:
			fprintf(stderr, "usage: getfacl [-ad] file ...\n");
			return 2;
		}
	}
	argc -= optind; argv += optind;
	if (argc == 0) { fprintf(stderr, "usage: getfacl [-ad] file ...\n");
	                 return 2; }
	while (argc-- > 0) rc |= show(*argv++);
	return rc;
}
