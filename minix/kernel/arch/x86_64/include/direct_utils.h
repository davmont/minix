#ifndef MB_UTILS_H
#define MB_UTILS_H

#include "kernel/kernel.h"

void direct_cls(void);
void direct_print(const char*);
void direct_print_char(char);
void direct_print_hex64(unsigned long);
int direct_read_char(unsigned char*);

#endif
