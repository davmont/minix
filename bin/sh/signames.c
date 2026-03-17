#define _NETBSD_SOURCE
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"
#include "trap.h"

struct signame_ent {
    int signo;
    const char *name;
};

static const struct signame_ent signames[] = {
#ifdef SIGHUP
    { SIGHUP, "HUP" },
#endif
#ifdef SIGINT
    { SIGINT, "INT" },
#endif
#ifdef SIGQUIT
    { SIGQUIT, "QUIT" },
#endif
#ifdef SIGILL
    { SIGILL, "ILL" },
#endif
#ifdef SIGTRAP
    { SIGTRAP, "TRAP" },
#endif
#ifdef SIGABRT
    { SIGABRT, "ABRT" },
#endif
#ifdef SIGEMT
    { SIGEMT, "EMT" },
#endif
#ifdef SIGFPE
    { SIGFPE, "FPE" },
#endif
#ifdef SIGKILL
    { SIGKILL, "KILL" },
#endif
#ifdef SIGBUS
    { SIGBUS, "BUS" },
#endif
#ifdef SIGSEGV
    { SIGSEGV, "SEGV" },
#endif
#ifdef SIGSYS
    { SIGSYS, "SYS" },
#endif
#ifdef SIGPIPE
    { SIGPIPE, "PIPE" },
#endif
#ifdef SIGALRM
    { SIGALRM, "ALRM" },
#endif
#ifdef SIGTERM
    { SIGTERM, "TERM" },
#endif
#ifdef SIGURG
    { SIGURG, "URG" },
#endif
#ifdef SIGSTOP
    { SIGSTOP, "STOP" },
#endif
#ifdef SIGTSTP
    { SIGTSTP, "TSTP" },
#endif
#ifdef SIGCONT
    { SIGCONT, "CONT" },
#endif
#ifdef SIGCHLD
    { SIGCHLD, "CHLD" },
#endif
#ifdef SIGTTIN
    { SIGTTIN, "TTIN" },
#endif
#ifdef SIGTTOU
    { SIGTTOU, "TTOU" },
#endif
#ifdef SIGIO
    { SIGIO, "IO" },
#endif
#ifdef SIGXCPU
    { SIGXCPU, "XCPU" },
#endif
#ifdef SIGXFSZ
    { SIGXFSZ, "XFSZ" },
#endif
#ifdef SIGVTALRM
    { SIGVTALRM, "VTALRM" },
#endif
#ifdef SIGPROF
    { SIGPROF, "PROF" },
#endif
#ifdef SIGWINCH
    { SIGWINCH, "WINCH" },
#endif
#ifdef SIGINFO
    { SIGINFO, "INFO" },
#endif
#ifdef SIGUSR1
    { SIGUSR1, "USR1" },
#endif
#ifdef SIGUSR2
    { SIGUSR2, "USR2" },
#endif
#ifdef SIGPWR
    { SIGPWR, "PWR" },
#endif
    { 0, NULL }
};

const char *
signalname(int signo)
{
    const struct signame_ent *sn;
    for (sn = signames; sn->name; sn++) {
        if (sn->signo == signo)
            return sn->name;
    }
    return NULL;
}

int
signalnumber(const char *name)
{
    const struct signame_ent *sn;
    const char *p = name;
    if (strncasecmp(p, "SIG", 3) == 0)
        p += 3;
    for (sn = signames; sn->name; sn++) {
        if (strcasecmp(sn->name, p) == 0)
            return sn->signo;
    }
    return 0;
}

int
signalnext(int signo)
{
    const struct signame_ent *sn;
    if (signo == 0) return signames[0].signo;
    for (sn = signames; sn->name; sn++) {
        if (sn->signo == signo) {
            if ((sn+1)->name)
                return (sn+1)->signo;
            break;
        }
    }
    return 0;
}
