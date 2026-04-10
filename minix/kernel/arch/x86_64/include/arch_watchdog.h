#ifndef __AMD64_WATCHDOG_H__
#define __AMD64_WATCHDOG_H__

#include "kernel/kernel.h"

/*
 * The NMI frame layout matches struct exception_frame (arch_proto.h):
 *   vector, errcode, rip, cs, rflags, rsp, ss
 * We can safely cast exception_frame* to nmi_frame* when calling
 * nmi_watchdog_handler from exception_handler.
 */
struct nmi_frame {
	reg_t   vector;
	reg_t   errcode;
	reg_t   rip;    /* program counter */
	reg_t   cs;
	reg_t   rflags;
	reg_t   rsp;
	reg_t   ss;
};

int amd64_watchdog_start(void);

#define nmi_in_kernel(f)	((f)->cs == KERN_CS_SELECTOR)

#endif /* __AMD64_WATCHDOG_H__ */
