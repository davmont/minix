/*
 * This file was manually recreated because it was missing in the source tree
 * and no generator script (mkoptions.sh) was found.
 * 
 * It defines the shell options for NetBSD 10 sync.
 */

#ifdef DEFINE_OPTIONS
#define DEF_OPTS(name, letter, opt_set, dflt) { name, letter, opt_set, 0, dflt },
struct optent optlist[] = {
#else
#define DEF_OPTS(name, letter, opt_set, dflt)
#endif

#define DEF_OPT(name, letter, dflt) DEF_OPTS(name, letter, 0, dflt)

DEF_OPT( "allexport", 'a', 0 )
DEF_OPT( "notify", 'b', 0 )
DEF_OPT( "noclobber", 'C', 0 )
DEF_OPTS( "emacs", 'E', 'V', 0 )
DEF_OPT( "errexit", 'e', 0 )
DEF_OPT( "fork", 'F', 0 )
DEF_OPT( "noglob", 'f', 0 )
DEF_OPT( "trackall", 'h', 0 )
DEF_OPT( "ignoreeof", 'I', 0 )
DEF_OPT( "interactive", 'i', 0 )
DEF_OPT( "local_lineno", 'L', 0 )
DEF_OPT( "login", 'l', 0 )
DEF_OPT( "monitor", 'm', 0 )
DEF_OPT( "noexec", 'n', 0 )
DEF_OPT( "nopriv", 'p', 0 )
DEF_OPT( "quietprofile", 'q', 0 )
DEF_OPT( "stdin", 's', 0 )
DEF_OPT( "nounset", 'u', 0 )
DEF_OPTS( "vi", 'V', 'V', 0 )
DEF_OPT( "verbose", 'v', 0 )
DEF_OPT( "xtrace", 'x', 0 )
DEF_OPT( "cdprint", 0, 0 )
DEF_OPT( "nolog", 0, 0 )
DEF_OPT( "pipefail", 0, 0 )
DEF_OPT( "posix", 0, 0 )
DEF_OPT( "promptcmds", 0, 0 )
DEF_OPT( "tabcomplete", 0, 0 )
DEF_OPT( "debug", 'D', 0 )
DEF_OPT( "fnline1", 0, 0 )
DEF_OPT( "xtracefd", 'X', 0 )

#ifdef DEFINE_OPTIONS
	{ 0, 0, 0, 0, 0 }
};

#define NOPTS (sizeof optlist / sizeof optlist[0] - 1)
int sizeof_optlist = sizeof optlist;

const unsigned char optorder[] = {
	0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 19, 20, 29
};
#define option_flags (sizeof optorder / sizeof optorder[0])

#else
extern struct optent optlist[];
extern int sizeof_optlist;
#define NOPTS (sizeof_optlist / (int)sizeof(struct optent) - 1)
extern const unsigned char optorder[];
#define option_flags 19
#endif

#define aflag optlist[0].val
#define bflag optlist[1].val
#define Cflag optlist[2].val
#define Eflag optlist[3].val
#define eflag optlist[4].val
#define usefork optlist[5].val
#define fflag optlist[6].val
#define hflag optlist[7].val
#define Iflag optlist[8].val
#define iflag optlist[9].val
#define Lflag optlist[10].val
#define loginsh optlist[11].val
#define mflag optlist[12].val
#define nflag optlist[13].val
#define pflag optlist[14].val
#define qflag optlist[15].val
#define sflag optlist[16].val
#define uflag optlist[17].val
#define Vflag optlist[18].val
#define vflag optlist[19].val
#define xflag optlist[20].val
#define cdprint optlist[21].val
#define nolog optlist[22].val
#define pipefail optlist[23].val
#define posix optlist[24].val
#define promptcmds optlist[25].val
#define tabcomplete optlist[26].val
#define debug optlist[27].val
#define fnline1 optlist[28].val
#define Xflag optlist[29].val

#define _SH_OPT_xflag 20
#define _SH_OPT_Xflag 29
