
#include <minix/minlib.h>
#include <minix/cpufeature.h>
#include <machine/partition.h>
#include "string.h"
#include "direct_utils.h"
#include "serial.h"
#include "glo.h"

/* Give non-zero values to avoid them in BSS */
static int print_line = 1, print_col = 1;

#include <sys/video.h>

extern char *video_mem;
#define VIDOFFSET(line, col) ((line) * MULTIBOOT_CONSOLE_COLS * 2 + (col) * 2)
#define VIDSIZE VIDOFFSET(MULTIBOOT_CONSOLE_LINES-1,MULTIBOOT_CONSOLE_COLS-1)

void direct_put_char(char c, int line, int col) 
{
	int offset = VIDOFFSET(line, col);
	video_mem[offset] = c;
	video_mem[offset+1] = 0x07;	/* grey-on-black */
}

static char direct_get_char(int line, int col) 
{
	return video_mem[VIDOFFSET(line, col)];
}

void direct_cls(void)
{
	/* Clear screen */
	int i,j;

	for(i = 0; i < MULTIBOOT_CONSOLE_COLS; i++)
		for(j = 0; j < MULTIBOOT_CONSOLE_LINES; j++)
			direct_put_char(' ', j, i);

	print_line = print_col = 0;

	/* Tell video hardware origin is 0. */
	outb(C_6845+INDEX, VID_ORG);
	outb(C_6845+DATA, 0);
	outb(C_6845+INDEX, VID_ORG+1);
	outb(C_6845+DATA, 0);
}

static void direct_scroll_up(int lines) 
{
	int i, j;
	for (i = 0; i < MULTIBOOT_CONSOLE_LINES; i++ ) {
		for (j = 0; j < MULTIBOOT_CONSOLE_COLS; j++ ) {
			char c = 0;
			if(i < MULTIBOOT_CONSOLE_LINES-lines)
				c = direct_get_char(i + lines, j);
			direct_put_char(c, i, j);
		}
	}
	print_line-= lines;
}

static void serial_putc(char c)
{
	int i;
	/* Wait for transmitter to be empty, then send. */
	for (i = 0; i < 100000 && !(inb(COM1_LSR) & LSR_THRE); i++) { }
	outb(COM1_THR, c);
	if (c == '\n') {
		for (i = 0; i < 100000 && !(inb(COM1_LSR) & LSR_THRE); i++) { }
		outb(COM1_THR, '\r');
	}
}

void direct_print_char(char c)
{
	serial_putc(c);

	while (print_line >= MULTIBOOT_CONSOLE_LINES) {
		outb(COM1_THR, 'S'); /* DBG: scroll */
		direct_scroll_up(1);
	}

#define TABWIDTH 8
	if(c == '\t') {
		if(print_col >= MULTIBOOT_CONSOLE_COLS - TABWIDTH) {
			c = '\n';
		} else {
			do {
				direct_put_char(' ', print_line, print_col++);
			} while(print_col % TABWIDTH);
			return;
		}
	}

	if (c == '\n') {
		outb(COM1_THR, '>'); /* DBG: enter nl-fill */
		while (print_col < MULTIBOOT_CONSOLE_COLS)
			direct_put_char(' ', print_line, print_col++);
		print_line++;
		print_col = 0;
		outb(COM1_THR, '<'); /* DBG: nl-fill done */
		return;
	}

	direct_put_char(c, print_line, print_col++);

	if (print_col >= MULTIBOOT_CONSOLE_COLS) {
		print_line++;
		print_col = 0;
	}

	while (print_line >= MULTIBOOT_CONSOLE_LINES)
		direct_scroll_up(1);
}

void direct_print(const char *str)
{
	while (*str) {
		direct_print_char(*str);
		str++;
	}
}

void direct_print_hex64(unsigned long v)
{
	static const char hex[] = "0123456789abcdef";
	char buf[19];
	int i;
	buf[0] = '0'; buf[1] = 'x';
	for (i = 0; i < 16; i++)
		buf[2 + i] = hex[(v >> (60 - i * 4)) & 0xf];
	buf[18] = '\0';
	direct_print(buf);
}

/* Standard and AT keyboard.  (PS/2 MCA implies AT throughout.) */
#define KEYBD		0x60	/* I/O port for keyboard data */
#define KB_STATUS	0x64	/* I/O port for status on AT */
#define KB_OUT_FULL	0x01	/* status bit set when keypress char pending */
#define KB_AUX_BYTE	0x20	/* Auxiliary Device Output Buffer Full */

int direct_read_char(unsigned char *ch)
{
	unsigned long sb;

	sb = inb(KB_STATUS);

	if (!(sb & KB_OUT_FULL)) {
		return 0;
	}

	inb(KEYBD);

	if (!(sb & KB_AUX_BYTE))
		return 1;

	return 0;
}

