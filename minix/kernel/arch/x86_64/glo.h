#ifndef __GLO_X86_64_H__
#define __GLO_X86_64_H__

#include "kernel/kernel.h"
#include "arch_proto.h"

EXTERN int cpu_has_tsc;	/* signal whether this cpu has a timestamp counter */

EXTERN struct tss_s tss[CONFIG_MAX_CPUS];

#endif /* __GLO_X86_64_H__ */
