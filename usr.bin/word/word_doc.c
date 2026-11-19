/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * word_doc.c: reading and writing Word 5.1 document files (SPEC.md, Word
 * document files).
 *
 * The file is 128-byte pages. Page 0 is the header, the text starts at
 * byte 0x80, and after it come pages of character runs and then pages of
 * paragraph runs. A run page (a formatted disk page) holds the file
 * offset its first run starts at, then six bytes per run (the end offset
 * and the page offset of the run's property record, or 0xffff for the
 * default), paragraph pages six more bytes per run of height estimates,
 * the property records growing down from byte 126, and the run count in
 * byte 127. A property record is a length and the leading bytes of the
 * property structure; the omitted bytes take their defaults.
 *
 * A paragraph mark is CR LF in the file and LF in the document. Text is
 * copied into the add stream as it loads, so the document does not keep
 * the file open.
 */

#include "word.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PG		128
#define FC0		0x80		/* file offset of the first text byte */
#define MAGIC		0xbe31
#define TOOL		0xab00
#define CHARSET		0x229b		/* ISO 8859-1 text */
#define NOPROP		0xffff
#define CHP_LEN		6
#define PAP_LEN		16
#define PAP_STYLE	0x3c		/* the Normal paragraph style */

static unsigned
get16(const uint8_t *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t
get32(const uint8_t *p)
{
	return get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void
put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
	put16(p, v & 0xffff);
	put16(p + 2, v >> 16);
}

static int
read_at(int fd, uint32_t off, void *buf, size_t n)
{
	ssize_t k;

	if (lseek(fd, (off_t)off, SEEK_SET) < 0)
		return errno;
	k = read(fd, buf, n);
	if (k < 0)
		return errno;
	return (size_t)k == n ? 0 : EINVAL;
}

static int
header_ok(const uint8_t *h)
{
	return get16(h) == MAGIC && get16(h + 2) == 0 && get16(h + 4) == TOOL;
}

int
doc_probe(const char *path)
{
	uint8_t h[PG];
	int fd = open(path, O_RDONLY), r;

	if (fd < 0)
		return -1;
	r = read_at(fd, 0, h, sizeof(h)) == 0 && header_ok(h);
	close(fd);
	return r;
}

/* ---- property records ---- */

static void
chp_default(uint8_t *b)
{
	memset(b, 0, CHP_LEN);
	b[2] = 24;
}

static void
pap_default(uint8_t *b)
{
	memset(b, 0, PAP_LEN);
	b[0] = PAP_STYLE;
	b[2] = 0x1e;
	put16(b + 10, 240);
}

static void
chp_decode(const uint8_t *b, struct word_chp *c)
{
	memset(c, 0, sizeof(*c));
	if (b[1] & 1)
		c->flags |= CH_BOLD;
	if (b[1] & 2)
		c->flags |= CH_ITALIC;
	if (b[3] & 0x01)
		c->flags |= CH_UNDER;
	if (b[3] & 0x02)
		c->flags |= CH_STRIKE;
	if (b[3] & 0x04)
		c->flags |= CH_DUNDER;
	if (b[3] & 0x20)
		c->flags |= CH_SMALLCAPS;
	else if (b[3] & 0x10)
		c->flags |= CH_UPPER;
	if (b[3] & 0x80)
		c->flags |= CH_HIDDEN;
	c->size = b[2] == 24 ? 0 : b[2];
	c->pos = b[5] == 0 ? POS_NORMAL : (b[5] & 0x80) ? POS_SUB : POS_SUPER;
}

static void
chp_encode(const struct word_chp *c, uint8_t *b)
{
	chp_default(b);
	b[1] = (uint8_t)((c->flags & CH_BOLD ? 1 : 0) | (c->flags & CH_ITALIC ? 2 : 0));
	if (c->size != 0)
		b[2] = c->size;
	b[3] = (uint8_t)((c->flags & CH_UNDER ? 0x01 : 0) |
	    (c->flags & CH_STRIKE ? 0x02 : 0) | (c->flags & CH_DUNDER ? 0x04 : 0) |
	    (c->flags & CH_UPPER ? 0x10 : 0) |
	    (c->flags & CH_SMALLCAPS ? 0x30 : 0) |
	    (c->flags & CH_HIDDEN ? 0x80 : 0));
	b[5] = c->pos == POS_SUPER ? 12 : c->pos == POS_SUB ? 0xf4 : 0;
}

static void
pap_decode(const uint8_t *b, struct word_pap *p)
{
	unsigned sp = get16(b + 10);

	memset(p, 0, sizeof(*p));
	p->align = b[1] & 3;
	p->keep = (uint8_t)((b[1] & 4 ? PA_KEEP : 0) | (b[1] & 8 ? PA_FOLLOW : 0) |
	    (b[1] & 0x10 ? PA_SIDE : 0));
	p->right = (int16_t)get16(b + 4);
	p->left = (int16_t)get16(b + 6);
	p->first = (int16_t)get16(b + 8);
	p->spacing = (uint16_t)(sp == 240 ? 0 : sp);
	p->before = (uint16_t)get16(b + 12);
	p->after = (uint16_t)get16(b + 14);
}

static void
pap_encode(const struct word_pap *p, uint8_t *b)
{
	pap_default(b);
	b[1] = (uint8_t)(p->align | (p->keep & PA_KEEP ? 4 : 0) |
	    (p->keep & PA_FOLLOW ? 8 : 0) | (p->keep & PA_SIDE ? 0x10 : 0));
	put16(b + 4, (unsigned)(uint16_t)p->right);
	put16(b + 6, (unsigned)(uint16_t)p->left);
	put16(b + 8, (unsigned)(uint16_t)p->first);
	put16(b + 10, p->spacing ? p->spacing : 240);
	put16(b + 12, p->before);
	put16(b + 14, p->after);
}

/* The length of the shortest record for b: through its last non-default byte. */
static unsigned
rec_len(const uint8_t *b, const uint8_t *dflt, unsigned n)
{
	while (n > 0 && b[n - 1] == dflt[n - 1])
		n--;
	return n;
}

/* ---- loading ---- */

struct runs {
	int fd, para;
	unsigned page, last;	/* next page to read, first page past the group */
	uint8_t pg[PG];
	unsigned i, n;
	uint32_t lim;
	unsigned prop;		/* property index of the current run */
};

static int
runs_page(struct runs *r)
{
	int e;

	if (r->page >= r->last)
		return EINVAL;
	e = read_at(r->fd, (uint32_t)r->page * PG, r->pg, PG);
	if (e != 0)
		return e;
	r->page++;
	r->n = r->pg[PG - 1];
	r->i = 0;
	if (r->n == 0 || 4 + (r->para ? 12u : 6u) * r->n > PG - 1)
		return EINVAL;
	return 0;
}

/* Advance to the run holding file offset fc and set r->prop. */
static int
runs_seek(struct runs *r, uint32_t fc)
{
	uint8_t full[PAP_LEN], dflt[PAP_LEN];
	unsigned len = r->para ? PAP_LEN : CHP_LEN, b, cch;
	int e, idx;

	while (fc >= r->lim) {
		if (r->i >= r->n && (e = runs_page(r)) != 0)
			return e;
		r->lim = get32(r->pg + 4 + 6 * r->i);
		b = get16(r->pg + 8 + 6 * r->i);
		r->i++;
		if (r->para)
			pap_default(dflt);
		else
			chp_default(dflt);
		memcpy(full, dflt, len);
		if (b != NOPROP) {
			b = (b & 0xff) + 4;
			if (b >= PG - 1)
				return EINVAL;
			cch = r->pg[b];
			if (b + 1 + cch > PG - 1)
				return EINVAL;
			memcpy(full, r->pg + b + 1, cch < len ? cch : len);
		}
		if (r->para) {
			struct word_pap p;

			pap_decode(full, &p);
			idx = buf_pap_index(&p);
		} else {
			struct word_chp c;

			chp_decode(full, &c);
			idx = buf_chp_index(&c);
		}
		if (idx < 0)
			return EFBIG;
		r->prop = (unsigned)idx;
	}
	return 0;
}

int
doc_load(const char *path)
{
	uint8_t h[PG];
	char text[PG], out[PG];
	struct runs cr, pr;
	struct stat st;
	uint32_t fcmac, fc, chunk;
	unsigned pnpara, pnmac, i, nout = 0, oc = 0, op = 0;
	int fd, e = 0, cr_pending = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno;
	if (fstat(fd, &st) != 0 || read_at(fd, 0, h, PG) != 0 || !header_ok(h) ||
	    get16(h + 0x7e) != CHARSET) {
		close(fd);
		return EINVAL;
	}
	fcmac = get32(h + 0x0e);
	pnpara = get16(h + 0x12);
	pnmac = get16(h + 0x6a);
	if (fcmac < FC0 || (uint32_t)pnmac * PG > (uint32_t)st.st_size ||
	    pnpara < (fcmac + PG - 1) / PG || pnpara >= pnmac) {
		close(fd);
		return EINVAL;
	}
	buf_clear();
	memset(&cr, 0, sizeof(cr));
	memset(&pr, 0, sizeof(pr));
	cr.fd = pr.fd = fd;
	cr.page = (fcmac + PG - 1) / PG;
	cr.last = pnpara;
	pr.para = 1;
	pr.page = pnpara;
	pr.last = pnmac;
	for (fc = FC0; e == 0 && fc < fcmac; fc += chunk) {
		chunk = fcmac - fc < PG ? fcmac - fc : PG;
		e = read_at(fd, fc, text, chunk);
		for (i = 0; e == 0 && i < chunk; i++) {
			char c = text[i];

			if ((e = runs_seek(&cr, fc + i)) != 0 ||
			    (e = runs_seek(&pr, fc + i)) != 0)
				break;
			if (c == '\r') {
				cr_pending = 1;
				continue;
			}
			if (nout > 0 && (nout + 2 > sizeof(out) || cr.prop != oc ||
			    pr.prop != op)) {
				e = buf_append_props(out, nout, oc, op);
				nout = 0;
			}
			oc = cr.prop;
			op = pr.prop;
			/* A CR without its LF still ends a paragraph. */
			if (cr_pending && c != '\n')
				out[nout++] = '\n';
			cr_pending = 0;
			out[nout++] = c;
		}
	}
	if (e == 0 && cr_pending) {
		if (nout + 1 > sizeof(out)) {
			e = buf_append_props(out, nout, oc, op);
			nout = 0;
		}
		out[nout++] = '\n';
	}
	if (e == 0 && nout > 0)
		e = buf_append_props(out, nout, oc, op);
	close(fd);
	if (e != 0) {
		buf_clear();
		return e;
	}
	buf_loaded();
	return 0;
}

/* ---- saving ---- */

/* A run page being filled; records grow down from byte 126. */
struct fkp {
	int fd, para, e;
	uint8_t pg[PG];
	unsigned n, top, page;
	uint32_t first, lim;
	unsigned prop, nrec;
	uint8_t rec_prop[20], rec_off[20];
};

static void
fkp_start(struct fkp *f, uint32_t fc)
{
	memset(f->pg, 0, PG);
	f->n = 0;
	f->top = PG - 1;
	f->nrec = 0;
	f->first = fc;
	put32(f->pg, fc);
}

static void
fkp_flush(struct fkp *f)
{
	f->pg[PG - 1] = (uint8_t)f->n;
	if (f->e == 0 && write(f->fd, f->pg, PG) != PG)
		f->e = errno != 0 ? errno : EIO;
	f->page++;
}

/* Add the run [.., lim) with property index prop. */
static void
fkp_add(struct fkp *f, uint32_t lim, unsigned prop)
{
	uint8_t full[PAP_LEN], dflt[PAP_LEN];
	unsigned len, i, off = NOPROP, entry = f->para ? 12 : 6;

	if (f->para) {
		pap_encode(buf_pap(prop), full);
		pap_default(dflt);
		len = rec_len(full, dflt, PAP_LEN);
	} else {
		chp_encode(buf_chp(prop), full);
		chp_default(dflt);
		len = rec_len(full, dflt, CHP_LEN);
	}
	for (;;) {
		unsigned need = 0;

		if (len > 0) {
			off = NOPROP;
			for (i = 0; i < f->nrec; i++)
				if (f->rec_prop[i] == prop)
					off = f->rec_off[i];
			if (off == NOPROP)
				need = len + 1;
		}
		if (4 + entry * (f->n + 1) + need <= f->top &&
		    (need == 0 || f->nrec < sizeof(f->rec_prop)))
			break;
		fkp_flush(f);
		fkp_start(f, f->lim);
	}
	if (len > 0 && off == NOPROP) {
		f->top -= len + 1;
		f->pg[f->top] = (uint8_t)len;
		memcpy(f->pg + f->top + 1, full, len);
		off = f->top - 4;
		f->rec_prop[f->nrec] = (uint8_t)prop;
		f->rec_off[f->nrec++] = (uint8_t)off;
	}
	/* Entries: n end offsets with record pointers, then the PHEs. */
	if (f->para)
		memmove(f->pg + 4 + 6 * (f->n + 1), f->pg + 4 + 6 * f->n, 6 * f->n);
	put32(f->pg + 4 + 6 * f->n, lim);
	put16(f->pg + 8 + 6 * f->n, len > 0 ? off : NOPROP);
	f->n++;
	f->lim = lim;
}

/* Write the text with each LF as CR LF; *fcmac gets the end offset. */
static int
emit_text(int fd, uint32_t *fcmac)
{
	char out[PG];
	woff_t p, len = buf_len();
	uint32_t fc = FC0;
	unsigned n = 0;
	int c;

	for (p = 0; p < len; p++) {
		c = buf_at(p);
		if (c < 0)
			return EIO;
		if (c == '\n') {
			out[n++] = '\r';
			fc++;
			if (n == sizeof(out)) {
				if (write(fd, out, n) != (ssize_t)n)
					return errno;
				n = 0;
			}
		}
		out[n++] = (char)c;
		fc++;
		if (n == sizeof(out)) {
			if (write(fd, out, n) != (ssize_t)n)
				return errno;
			n = 0;
		}
	}
	if (n > 0 && write(fd, out, n) != (ssize_t)n)
		return errno;
	*fcmac = fc;
	return 0;
}

static int
emit_runs(int fd, int para, uint32_t fcmac, unsigned *page)
{
	struct fkp f;
	woff_t p, len = buf_len();
	uint32_t fc = FC0;
	int have = 0, v, c;
	unsigned prop = 0, cur;

	memset(&f, 0, sizeof(f));
	f.fd = fd;
	f.para = para;
	f.page = *page;
	f.lim = FC0;
	fkp_start(&f, FC0);
	for (p = 0; p < len; p++) {
		/* A paragraph run ends after its mark; the mark decides it. */
		if (para) {
			woff_t q = p;

			while (q < len && buf_at(q) != '\n')
				q++;
			v = buf_props(q < len ? q : len - 1);
			cur = (unsigned)v >> 8;
			if (have && cur != prop)
				fkp_add(&f, fc, prop);
			prop = cur;
			have = 1;
			fc += (uint32_t)(q - p) + (q < len ? 2 : 0);
			p = q;
			continue;
		}
		v = buf_props(p);
		c = buf_at(p);
		cur = (unsigned)(v & 0xff);
		if (have && cur != prop)
			fkp_add(&f, fc, prop);
		prop = cur;
		have = 1;
		fc += c == '\n' ? 2 : 1;
	}
	if (para)
		fkp_add(&f, fcmac + 1, have ? prop : 0);
	else
		fkp_add(&f, fcmac, have ? prop : 0);
	fkp_flush(&f);
	*page = f.page;
	return f.e;
}

int
doc_save(const char *path, int backup)
{
	char tmp[WORD_PATH_MAX + 8], bak[WORD_PATH_MAX + 2];
	uint8_t h[PG], pad[PG];
	struct stat st;
	uint32_t fcmac = FC0;
	unsigned page, pnpara, slots[] = { 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x76,
	    0x78, 0x7c }, i;
	int fd, e, have = stat(path, &st) == 0;

	if (strlen(path) >= WORD_PATH_MAX)
		return ENAMETOOLONG;
	snprintf(tmp, sizeof(tmp), "%s.wtmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return errno;
	memset(h, 0, PG);
	e = write(fd, h, PG) == PG ? 0 : EIO;
	if (e == 0)
		e = emit_text(fd, &fcmac);
	if (e == 0 && (fcmac % PG) != 0) {
		memset(pad, 0, PG);
		if (write(fd, pad, PG - fcmac % PG) != (ssize_t)(PG - fcmac % PG))
			e = EIO;
	}
	page = (fcmac + PG - 1) / PG;
	if (e == 0)
		e = emit_runs(fd, 0, fcmac, &page);
	pnpara = page;
	if (e == 0)
		e = emit_runs(fd, 1, fcmac, &page);
	if (e == 0) {
		put16(h, MAGIC);
		put16(h + 4, TOOL);
		put32(h + 0x0e, fcmac);
		put16(h + 0x12, pnpara);
		for (i = 0; i < sizeof(slots) / sizeof(slots[0]); i++)
			put16(h + slots[i], page);
		memcpy(h + 0x1e, "normal.stx", 10);
		memcpy(h + 0x62, "tty", 3);
		put16(h + 0x6a, page);
		put16(h + 0x6c, 0x40);
		put16(h + 0x70, 720);
		put16(h + 0x74, 0x105);
		put16(h + 0x7e, CHARSET);
		if (lseek(fd, 0, SEEK_SET) != 0 || write(fd, h, PG) != PG)
			e = EIO;
	}
	if (have && e == 0 && fchmod(fd, st.st_mode & 07777) != 0)
		e = errno;
	if (close(fd) != 0 && e == 0)
		e = errno;
	if (e == 0 && have && backup) {
		snprintf(bak, sizeof(bak), "%s~", path);
		unlink(bak);
		if (link(path, bak) != 0)
			e = errno;
	}
	if (e == 0 && rename(tmp, path) != 0)
		e = errno;
	if (e != 0)
		unlink(tmp);
	return e;
}

int
doc_formatted(void)
{
	woff_t p, len = buf_len();

	for (p = 0; p < len; p++)
		if (buf_props(p) != 0)
			return 1;
	return 0;
}
