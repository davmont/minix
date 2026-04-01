#ifndef STACK_FRAME_H
#define STACK_FRAME_H

#include <sys/types.h>

typedef u64_t reg_t;
typedef reg_t segdesc_t;

struct stackframe_s {
	u16_t gs;
	u16_t fs;
	u16_t es;
	u16_t ds;
	reg_t r15;
	reg_t r14;
	reg_t r13;
	reg_t r12;
	reg_t r11;
	reg_t r10;
	reg_t r9;
	reg_t r8;
	reg_t rbp;
	reg_t rdi;
	reg_t rsi;
	reg_t rdx;
	reg_t rcx;
	reg_t rbx;
	reg_t rax;
	reg_t retreg;
	reg_t pc;
	reg_t cs;
	reg_t psw;
	reg_t sp;
	reg_t ss;
};

#endif
