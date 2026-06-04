/* Utilities to generate a proper C stack.
 *
 * Author: Lionel A. Sambuc. 
 */

#define _MINIX_SYSTEM

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <sys/exec_elf.h>
#include <sys/exec.h>

/* Create a stack image that only needs to be patched up slightly by
 * the kernel to be used for the process to be executed.
 *
 * Every pointers are stored here as offset from the frame base, and
 * will be adapted as required for the new process address space.
 *
 * The following parameters are passed by register to either __start
 * for static binaries, or _rtld_start for dynamic ones:
 *     *fct, *ObjEntry, *ps_string
 *
 * The following stack layout is expected by _rtld():
 *
 * | XXXXXXXXXX | 0x0000_00000
 * |  ...       |
 * |  ...       | Top of the stack
 * | argc       |
 * | *argv1     | points to the first char of the argv1
 * |  ...       |
 * | *argvN     |
 * | NULL       |
 * | *env1      | 
 * |  ...       |
 * | *envN      |
 * | NULL       |
 * | ElfAuxV1   |
 * |  ...       |
 * | ElfAuxVX   |
 * | AuxExecName| fully resolve executable name, as an ASCIIZ string,
 *                at most PMEF_EXECNAMELEN1 long.
 * 
 * Here we put first the strings, then word-align, then ps_strings, to
 * comply with the expected layout of NetBSD. This seems to matter for
 * the NetBSD ps command, so let's make sure we are compatible...
 *
 * | strings    | Maybe followed by some padding to word-align.
 * | **argv     | \
 * | argc       |  +---> ps_string structure content.
 * | **env      |  |
 * | envc       | /
 * | sigcode    | On NetBSD, there may be a compatibility stub here,
 * +------------+    for native code, it is not present.
 *   Stack Base , 0xF000_0000, descending stack.
 */

/* The minimum size of the frame is composed of:
 * argc, the NULL terminator for argv as well as one for
 * environ, the ELF Aux vectors, executable name and the
 * ps_strings struct. */
#define STACK_MIN_SZ \
( \
	sizeof(void *) + sizeof(void *) * 2 + \
	sizeof(AuxInfo) * PMEF_AUXVECTORS + PMEF_EXECNAMELEN1 + \
	sizeof(struct ps_strings) \
)

/***************************************************************************** 
 * Computes stack size, argc, envc, for a given set of path, argv, envp.     *
 *****************************************************************************/
void minix_stack_params(const char *path, char * const *argv, char * const *envp,
	size_t *stack_size,  char *overflow, int *argc, int *envc)
{
	char * const *p;
	size_t const min_size = STACK_MIN_SZ;

	*stack_size = min_size;	/* Size of the new initial stack. */
	*overflow = 0;		/* No overflow yet. */
	*argc = 0;		/* Argument count. */
	*envc = 0;		/* Environment count */

	/* Compute and add the size required to store argv and env. */
	for (p = argv; *p != NULL; p++) {
		size_t const n = sizeof(*p) + strlen(*p) + 1;
		*stack_size += n;
		if (*stack_size < n) {
			*overflow = 1;
		}
		(*argc)++;
	}

	for (p = envp; p && *p != NULL; p++) {
		size_t const n = sizeof(*p) + strlen(*p) + 1;
		*stack_size += n;
		if (*stack_size < n) {
			*overflow = 1;
		}
		(*envc)++;
	}

	/* Compute the aligned frame size.  The frame is placed at the very top
	 * of the user stack (vsp = user_sp - stack_size), and argc sits at vsp.
	 * The SysV ABI requires %rsp to be 16-byte aligned at process entry, so
	 * the frame size must be a multiple of 16 — not just sizeof(void *).
	 * Otherwise amd64 binaries fault on the first movaps to a stack slot. */
	*stack_size = (*stack_size + 16 - 1) & ~(size_t)(16 - 1);

	if (*stack_size < min_size) {
		/* This is possible only in case of overflow. */
		*overflow = 1;
	}
}

/*****************************************************************************
 * Generate a stack in the buffer frame, ready to be used.                   *
 *****************************************************************************/
void minix_stack_fill(const char *path, int argc, char * const *argv,
	int envc, char * const *envp, size_t stack_size, char *frame,
	int *vsp, struct ps_strings **psp)
{
	char * const *p;

	/* Frame pointers (a.k.a stack pointer within the buffer in current
	 * address space.) */
	char *fp;	/* byte aligned */
	char **fpw;	/* word aligned */

	size_t const min_size = STACK_MIN_SZ;

	/* Virtual address of the stack pointer, in new memory space. */
	*vsp = minix_get_user_sp() - stack_size;

	/* Fill in the frame now. */
	fpw = (char **) frame;
	*fpw++ = (char *)(uintptr_t) argc;

	/* The strings themselves are stored after the aux vectors,
	 * cf. top comment. */
	fp = frame + (min_size - sizeof(struct ps_strings)) + 
		(envc + argc) * sizeof(char *);
	
	/* Fill in argv and the environment, as well as copy the strings
	 * themselves. */
	for (p = argv; *p != NULL; p++) {
		size_t const n = strlen(*p) + 1;
		*fpw++= (char *)(*vsp + (fp - frame));
		memcpy(fp, *p, n);
		fp += n;
	}
	*fpw++ = NULL;

	for (p = envp; p && *p != NULL; p++) {
		size_t const n = strlen(*p) + 1;
		*fpw++= (char *)(*vsp + (fp - frame));
		memcpy(fp, *p, n);
		fp += n;
	}
	*fpw++ = NULL;

	/* Padding to position ps_strings at exactly stack_size - sizeof(ps_strings).
	 *
	 * minix_stack_params rounds stack_size up to a 16-byte multiple (for the
	 * amd64 SysV ABI's %rsp alignment requirement at process entry), while the
	 * natural fp position after string writes is only sizeof(void *)-aligned.
	 * If we just padded to PTRSIZE, ps_strings could sit up to 8 bytes BEFORE
	 * the frame end, leaving an 8-byte slack at the very top of the frame.
	 *
	 * VFS's patch_stack(), used for #! shebang interpreter rewriting, locates
	 * ps_strings via `frame + frame_len - sizeof(struct ps_strings)`.  If
	 * libc's actual psp doesn't match that, patch_stack reads/writes the
	 * wrong bytes — the new process ends up with libc's stale ps_strings,
	 * pointing into the envp pointer region instead of argv, and either
	 * faults in _start or sees garbage argc/argv/envp.  The bug only fires
	 * when argv+envp string lengths land on a (argv_strs+envp_strs) % 8
	 * residue that causes pad_to_16 > pad_to_8.
	 *
	 * Padding fp up to exactly stack_size - sizeof(ps_strings) keeps libc
	 * and VFS in agreement.  No-op when there is no slack. */
	{
		char *psp_target = frame + stack_size - sizeof(struct ps_strings);
		while (fp < psp_target) *fp++ = 0;
	}

	/* Fill in the ps_string struct*/
	*psp = (struct ps_strings *) fp;

	(*psp)->ps_argvstr = (char **)(*vsp + sizeof(void *));
	(*psp)->ps_nargvstr = argc;
	(*psp)->ps_envstr = (*psp)->ps_argvstr + argc + 1;
	(*psp)->ps_nenvstr = envc;
}
