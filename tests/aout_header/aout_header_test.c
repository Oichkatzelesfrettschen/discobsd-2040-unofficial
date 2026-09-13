/*
 * Host test for the a.out header macros and the layout checker in
 * sys/sys/exec_aout.h. Every flag, machine id and a set of magic numbers
 * round-trip through N_SETMAGIC and the N_GET macros, every 32-bit word
 * decodes into fields that reassemble it, and a table of images exercises
 * each result of aout_layout_check.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/sys/exec_aout.h"

#define BASE    0x20000000U
#define WINDOW  (144U * 1024)

static int failures;

static void
check(int cond, const char *what)
{
	if (!cond) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static void
roundtrip(void)
{
	static const unsigned magics[] = { 0, RMAGIC, OMAGIC, NMAGIC, 0x1234, 0xffff };
	struct exec e;
	unsigned flag, mid, m, n = 0;

	for (flag = 0; flag < 64; flag++)
		for (mid = 0; mid < 1024; mid++)
			for (m = 0; m < sizeof magics / sizeof magics[0]; m++) {
				N_SETMAGIC(e, magics[m], mid, flag);
				if (N_GETFLAG(e) != flag || N_GETMID(e) != mid ||
				    N_GETMAGIC(e) != magics[m]) {
					failures++;
					printf("FAIL: roundtrip flag %u mid %u magic %#x -> %#x\n",
					    flag, mid, magics[m], e.a_midmag);
					return;
				}
				n++;
			}
	printf("roundtrip: %u encodings\n", n);
}

static void
decode_partition(void)
{
	struct exec e;
	unsigned w = 0x9e3779b9U, i;

	for (i = 0; i < 1000000; i++) {
		e.a_midmag = w;
		if (((N_GETFLAG(e) << 26) | (N_GETMID(e) << 16) | N_GETMAGIC(e)) != w ||
		    N_GETFLAG(e) > 63 || N_GETMID(e) > 1023 || N_GETMAGIC(e) > 0xffff) {
			failures++;
			printf("FAIL: decode of %#x does not partition\n", w);
			return;
		}
		w = w * 1664525U + 1013904223U;
	}
	printf("decode: %u words partition into flag, mid, magic\n", i);
}

static struct exec
image(unsigned text, unsigned data, unsigned bss, unsigned entry)
{
	struct exec e;

	memset(&e, 0, sizeof e);
	N_SETMAGIC(e, OMAGIC, MID_ZERO, 0);
	e.a_text = text;
	e.a_data = data;
	e.a_bss = bss;
	e.a_entry = entry;
	return e;
}

static void
layout(void)
{
	struct exec e;
	unsigned long full;

	e = image(0x70e4, 0x96c, 0x1b10, BASE + 0x1f5);
	full = sizeof(struct exec) + 0x70e4 + 0x96c;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_OK, "shipped keen header");
	check(aout_layout_check(&e, BASE, WINDOW, full - 1, 1) == AOUT_TRUNC, "file one byte short");
	check(aout_layout_check(&e, BASE, WINDOW, full, 0) == AOUT_BADENTRY, "odd entry without thumb is misaligned");
	e.a_entry = BASE + 0x1f4;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_BADENTRY, "even entry with thumb");
	check(aout_layout_check(&e, BASE, WINDOW, full, 0) == AOUT_OK, "even entry without thumb");
	e.a_entry = BASE + 0x70e4 + 1;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_BADENTRY, "entry at end of text");
	e.a_entry = BASE + 0x70e3;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_OK, "entry at last halfword");
	e.a_entry = BASE - 1;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_BADENTRY, "entry below base");
	e.a_entry = 1;
	check(aout_layout_check(&e, BASE, WINDOW, full, 1) == AOUT_BADENTRY, "entry near zero");

	e = image(0, 0x100, 0, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_BADENTRY, "no text");

	e = image(0xffffff00U, 0x200, 0, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_SIZEWRAP, "text + data wraps");
	e = image(0x100, 0x100, 0xffffffffU, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_SIZEWRAP, "image + bss wraps");
	e = image(0xfffffff0U, 0, 0, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_SIZEWRAP, "header + image wraps");

	e = image(WINDOW, 1, 0, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_TOOBIG, "one byte over the window");
	e = image(WINDOW - 0x100, 0x80, 0x80, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_OK, "exactly the window");
	e = image(0x100, 0x100, WINDOW, BASE + 1);
	check(aout_layout_check(&e, BASE, WINDOW, 0xffffffffUL, 1) == AOUT_TOOBIG, "bss alone over the window");

	e = image(0x100, 0x100, 0, BASE + 1);
	N_SETMAGIC(e, NMAGIC, MID_ZERO, 0);
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_BADMAG, "NMAGIC");
	N_SETMAGIC(e, OMAGIC, MID_ARM6, 0);
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_BADMAG, "machine id");
	N_SETMAGIC(e, OMAGIC, MID_ZERO, EX_PIC);
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_BADMAG, "flag bit");
	N_SETMAGIC(e, OMAGIC, MID_ZERO, 0);
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_OK, "flags cleared again");
	e.a_midmag = 0x107;
	check(aout_layout_check(&e, BASE, WINDOW, 0x1000, 1) == AOUT_OK, "raw shipped midmag 0x107");
}

int
main(void)
{
	roundtrip();
	decode_partition();
	layout();
	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("aout header: ok\n");
	return 0;
}
