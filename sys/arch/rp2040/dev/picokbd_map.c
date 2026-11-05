/*
 * Copyright (c) 2026 DiscoBSD
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Keyboard-controller key codes to xterm byte sequences; see
 * picokbd_map.h. The function-key and editing-key sequences are those of
 * the xterm entry in lib/libtermlib/termcap/termcap.small (kh, @7, kI, kD,
 * kP, kN, k1-k;), so termcap programs recognize every key. Enter sends
 * carriage return and Backspace sends DEL, as a terminal does, because the
 * tty layer maps CR to newline and erases on DEL (CERASE in ttychars.h).
 */

#include "picokbd_map.h"

#define	KEY_BACKSPACE	0x08
#define	KEY_TAB		0x09
#define	KEY_ENTER	0x0a
#define	KEY_MOD_CTRL	0xa5
#define	KEY_ESC		0xb1
#define	KEY_LEFT	0xb4
#define	KEY_UP		0xb5
#define	KEY_DOWN	0xb6
#define	KEY_RIGHT	0xb7
#define	KEY_INSERT	0xd1
#define	KEY_HOME	0xd2
#define	KEY_DEL		0xd4
#define	KEY_END		0xd5
#define	KEY_PAGE_UP	0xd6
#define	KEY_PAGE_DOWN	0xd7
#define	KEY_F1		0x81		/* F1-F9 are 0x81-0x89. */
#define	KEY_F10		0x90

/* F1-F10 as the xterm termcap entry's k1-k9 and k;. */
static const char *const fkeys[10] = {
	"\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~",
	"\033[17~", "\033[18~", "\033[19~", "\033[20~", "\033[21~",
};

static int
copy(char *out, const char *s)
{
	int n = 0;

	while (s[n] != '\0' && n < PICOKBD_MAXSEQ) {
		out[n] = s[n];
		n++;
	}
	return n;
}

static int
cursor_key(char *out, int final, int appcursor)
{
	out[0] = '\033';
	out[1] = appcursor ? 'O' : '[';
	out[2] = (char)final;
	return 3;
}

int
picokbd_translate(int state, int key, int appcursor, int *ctrl,
    char out[PICOKBD_MAXSEQ])
{
	key &= 0xff;
	if (key == KEY_MOD_CTRL) {
		if (state == PICOKBD_PRESSED || state == PICOKBD_HOLD)
			*ctrl = 1;
		else if (state == PICOKBD_RELEASED)
			*ctrl = 0;
		return 0;
	}
	if (state != PICOKBD_PRESSED)
		return 0;

	if (key >= 0x20 && key <= 0x7e) {
		if (*ctrl) {
			if (key >= 0x40)
				key &= 0x1f;	/* @ A-Z [ \ ] ^ _ and a-z. */
			else if (key == ' ')
				key = 0;
			else if (key == '?')
				key = 0x7f;
		}
		out[0] = (char)key;
		return 1;
	}

	switch (key) {
	case KEY_BACKSPACE:
		out[0] = 0x7f;
		return 1;
	case KEY_TAB:
		out[0] = '\t';
		return 1;
	case KEY_ENTER:
		out[0] = '\r';
		return 1;
	case KEY_ESC:
		out[0] = '\033';
		return 1;
	case KEY_UP:
		return cursor_key(out, 'A', appcursor);
	case KEY_DOWN:
		return cursor_key(out, 'B', appcursor);
	case KEY_RIGHT:
		return cursor_key(out, 'C', appcursor);
	case KEY_LEFT:
		return cursor_key(out, 'D', appcursor);
	case KEY_HOME:
		return cursor_key(out, 'H', appcursor);
	case KEY_END:
		return cursor_key(out, 'F', appcursor);
	case KEY_INSERT:
		return copy(out, "\033[2~");
	case KEY_DEL:
		return copy(out, "\033[3~");
	case KEY_PAGE_UP:
		return copy(out, "\033[5~");
	case KEY_PAGE_DOWN:
		return copy(out, "\033[6~");
	case KEY_F10:
		return copy(out, fkeys[9]);
	default:
		if (key >= KEY_F1 && key < KEY_F1 + 9)
			return copy(out, fkeys[key - KEY_F1]);
		return 0;	/* Shift, Alt, Sym, Caps Lock, power, joystick. */
	}
}
