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
 * ESP-PSRAM64H access over PIO0 state machine 0: pin and block setup, the
 * reset, identification and quad-mode entry sequence, and chip-select
 * frames of at most PSRAM_CHUNK bytes.
 *
 * The device refreshes itself only while CE# is high, so a frame may hold
 * CE# low for at most tCEM, 8 us (datasheet Table 10-5, page 17). The
 * state machine stalls rather than aborts when its FIFOs run dry, which
 * stretches a frame, so a frame is driven with interrupts masked and from
 * RAM: the TX FIFO is loaded before the state machine starts, and the RX
 * FIFO, four words deep, is drained at about three times the rate the
 * state machine fills it. A read whose RX FIFO overruns stalls the
 * state machine on a rising clock edge, which shifts the sampling window,
 * and FDEBUG.RXSTALL reports it so the frame is read again.
 */

#ifdef PSRAM_HOST
#include "psram_host.h"
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/intr.h>
#include <rp2040/dev/picocalc_reg.h>

#define	PSRAM_RD(off)		PC_REG32(PSRAM_PIO_BASE + (off))
#define	PSRAM_WR(off, v)	(PC_REG32(PSRAM_PIO_BASE + (off)) = (v))
#define	PSRAM_UNRESET()		pc_unreset(PC_RESET_IO_BANK0 | \
				    PC_RESET_PADS_BANK0 | (1UL << 10))
#define	PSRAM_PIN(n, pad)	pc_pin((n), PSRAM_FUNC_PIO0, (pad))
#define	PSRAM_DELAY_US(us)	do { \
		unsigned int t0_ = pc_micros(); \
		while ((unsigned int)(pc_micros() - t0_) < (us)) \
			continue; \
	} while (0)
#define	PSRAM_IRQ_SAVE()	splhigh()
#define	PSRAM_IRQ_RESTORE(s)	splx(s)
#define	PSRAM_RAMFUNC		__attribute__((noinline, section(".ramfunc")))
#endif

#include <rp2040/dev/psram_pio.h>
#include <rp2040/dev/psram_xfer.h>

#define	PSRAM_PAD_DRIVE		(0x2UL << 4)	/* 8 mA */
#define	PSRAM_PAD_FAST		0x1UL		/* fast slew rate */
#define	PSRAM_PAD_PDE		(1UL << 2)	/* pull-down */

#define	PSRAM_SPIN		1024
#define	PSRAM_RETRIES		2
#define	PSRAM_SELFTEST		32
#define	PSRAM_RECOVERY_US	150

#define	PSRAM_CMD_RSTEN		0x66
#define	PSRAM_CMD_RST		0x99
#define	PSRAM_CMD_READID	0x9f
#define	PSRAM_CMD_QUADEN	0x35
#define	PSRAM_CMD_QREAD		0xeb
#define	PSRAM_CMD_QWRITE	0x38
#define	PSRAM_READ_WAIT		6	/* datasheet 5.4, truth table, EBh */
#define	PSRAM_ID_WAIT		1	/* 0 cycles, one released early */

unsigned char psram_mfid, psram_kgd, psram_eid[PSRAM_EID_BYTES];
int psram_id_read;

/*
 * The frame buffers are file scope: a swap transfer runs on a kernel stack
 * with little to spare, and transfers never nest.
 */
static unsigned int psram_tx[2 + PSRAM_CHUNK / 4 + 1];
static unsigned int psram_rx[PSRAM_CHUNK / 4];

void
psram_hw_init(void)
{
	static const unsigned short prog[] = { PSRAM_PIO_WORDS };
	unsigned int i;

	PSRAM_UNRESET();
	PSRAM_WR(PSRAM_PIO_CTRL, 0);
	for (i = 0; i < PSRAM_PIO_LENGTH; i++)
		PSRAM_WR(PSRAM_PIO_INSTR_MEM + 4 * i, prog[i]);
	PSRAM_WR(PSRAM_PIO_SM_CLKDIV, (unsigned int)PSRAM_CLKDIV << 16);
	PSRAM_WR(PSRAM_PIO_SM_EXECCTRL, PSRAM_EXECCTRL);
	PSRAM_WR(PSRAM_PIO_SM_SHIFTCTRL, PSRAM_SHIFTCTRL);

	/*
	 * Drive CS high and SCK low from the state machine before the pins
	 * switch to the PIO function, so CS never falls while they connect.
	 */
	PSRAM_WR(PSRAM_PIO_SM_PINCTRL, PSRAM_PINCTRL_CSCK);
	PSRAM_WR(PSRAM_PIO_SM_INSTR, PSRAM_INSTR_SET_PINS_IDLE);
	PSRAM_WR(PSRAM_PIO_SM_INSTR, PSRAM_INSTR_SET_PINDIRS_CSCK);
	PSRAM_WR(PSRAM_PIO_SM_PINCTRL, PSRAM_PINCTRL);
	PSRAM_WR(PSRAM_PIO_SM_INSTR, PSRAM_INSTR_JMP_IDLE);

	for (i = 0; i < 4; i++)
		PSRAM_PIN(PSRAM_PIN_SIO0 + i,
		    PSRAM_PAD_DRIVE | PSRAM_PAD_FAST | PSRAM_PAD_PDE);
	PSRAM_PIN(PSRAM_PIN_CS, PSRAM_PAD_DRIVE | PSRAM_PAD_FAST);
	PSRAM_PIN(PSRAM_PIN_SCK, PSRAM_PAD_DRIVE | PSRAM_PAD_FAST);
}

/* Return the state machine to its idle point and empty both FIFOs. */
static PSRAM_RAMFUNC void
psram_recover(void)
{
	PSRAM_WR(PSRAM_PIO_CTRL, 0x10);	/* SM_RESTART, disabled */
	PSRAM_WR(PSRAM_PIO_SM_INSTR, PSRAM_INSTR_JMP_IDLE);
	PSRAM_WR(PSRAM_PIO_SM_SHIFTCTRL,
	    PSRAM_SHIFTCTRL | PSRAM_SHIFTCTRL_FJOIN_TX);
	PSRAM_WR(PSRAM_PIO_SM_SHIFTCTRL, PSRAM_SHIFTCTRL);
}

/*
 * One chip-select frame: send the words in tx, collect nrx words into rx.
 * The first four TX words go in before the state machine starts, so it
 * never waits for the words that carry CS low; it returns to its idle
 * point, CS high, before this returns.
 */
static PSRAM_RAMFUNC int
psram_frame(const unsigned int *tx, unsigned int ntx, unsigned int *rx,
    unsigned int nrx)
{
	unsigned int i, got, st, spin;
	int s, err = 0;

	s = PSRAM_IRQ_SAVE();
	PSRAM_WR(PSRAM_PIO_FDEBUG, PSRAM_FDEBUG_RXSTALL);
	for (i = 0; i < ntx && i < 4; i++)
		PSRAM_WR(PSRAM_PIO_TXF0, tx[i]);
	PSRAM_WR(PSRAM_PIO_CTRL, 1);

	got = 0;
	for (spin = PSRAM_SPIN; (i < ntx || got < nrx) && spin != 0; spin--) {
		st = PSRAM_RD(PSRAM_PIO_FSTAT);
		if (i < ntx && (st & PSRAM_FSTAT_TXFULL) == 0)
			PSRAM_WR(PSRAM_PIO_TXF0, tx[i++]);
		if (got < nrx && (st & PSRAM_FSTAT_RXEMPTY) == 0)
			rx[got++] = PSRAM_RD(PSRAM_PIO_RXF0);
	}
	if (i < ntx || got < nrx)
		err = PSRAM_E_TIMEOUT;
	else {
		for (spin = PSRAM_SPIN; spin != 0; spin--)
			if (PSRAM_RD(PSRAM_PIO_SM_ADDR) == 0 &&
			    (PSRAM_RD(PSRAM_PIO_FSTAT) &
			    PSRAM_FSTAT_TXEMPTY) != 0)
				break;
		if (spin == 0)
			err = PSRAM_E_TIMEOUT;
		else if (nrx != 0 && (PSRAM_RD(PSRAM_PIO_FDEBUG) &
		    PSRAM_FDEBUG_RXSTALL) != 0)
			err = PSRAM_E_STALL;
	}
	if (err != 0)
		psram_recover();
	else
		PSRAM_WR(PSRAM_PIO_CTRL, 0);
	PSRAM_IRQ_RESTORE(s);
	return err;
}

/* A serial-mode byte as eight nibbles carrying one bit each on SIO0. */
static unsigned int
psram_serial_word(unsigned int b)
{
	unsigned int w = 0, k;

	for (k = 0; k < 8; k++)
		w = (w << 4) | ((b >> (7 - k)) & 1);
	return w;
}

static int
psram_cmd_serial(unsigned int cmd)
{
	psram_tx[0] = PSRAM_CTRL_WORD(8, 0, 0);
	psram_tx[1] = psram_serial_word(cmd);
	return psram_frame(psram_tx, 2, psram_rx, 0);
}

static int
psram_cmd_quad(unsigned int cmd)
{
	psram_tx[0] = PSRAM_CTRL_WORD(2, 0, 0);
	psram_tx[1] = cmd << 24;
	return psram_frame(psram_tx, 2, psram_rx, 0);
}

/*
 * Read ID, serial mode: the command, then 23 address bits; the 24th
 * address clock, which the device ignores, is a turnaround clock so the
 * state machine has released SIO0 before the device drives SIO1 with the
 * first ID bit. Each nibble read carries one ID bit in its bit 1, so each
 * RX word holds one byte: the manufacturer, the known-good die, then the
 * six EID bytes. The 96-clock frame holds CS low about 3 us at 31.25 MHz,
 * inside tCEM, and under the 33 MHz the APS6404L sheet allows for 9Fh.
 */
static int
psram_read_id(void)
{
	unsigned char id[2 + PSRAM_EID_BYTES];
	unsigned int w, k, i, b;
	int err;

	psram_tx[0] = PSRAM_CTRL_WORD(31, PSRAM_ID_WAIT, 8 * sizeof(id));
	psram_tx[1] = psram_serial_word(PSRAM_CMD_READID);
	psram_tx[2] = psram_tx[3] = psram_tx[4] = 0;
	err = psram_frame(psram_tx, 5, psram_rx, sizeof(id));
	if (err != 0)
		return err;
	for (i = 0; i < sizeof(id); i++) {
		b = 0;
		for (k = 0; k < 8; k++) {
			w = (psram_rx[i] >> (28 - 4 * k)) & 0xf;
			b = (b << 1) | ((w >> 1) & 1);
		}
		id[i] = (unsigned char)b;
	}
	psram_mfid = id[0];
	psram_kgd = id[1];
	for (i = 0; i < PSRAM_EID_BYTES; i++)
		psram_eid[i] = id[2 + i];
	psram_id_read = 1;
	return 0;
}

/*
 * Frame length from addr: whole 64-byte blocks, so a frame never leaves a
 * 1 KB page and a swap segment that starts on a 1 KB block boundary
 * splits into full frames.
 */
unsigned int
psram_chunk_len(unsigned int addr, unsigned int len)
{
	unsigned int n = PSRAM_CHUNK - (addr & (PSRAM_CHUNK - 1));

	return n < len ? n : len;
}

static int
psram_read_chunk(unsigned int addr, unsigned char *dst, unsigned int n)
{
	unsigned int words = (n + 3) / 4, i, tries;
	int err = 0;

	psram_tx[0] = PSRAM_CTRL_WORD(8, PSRAM_READ_WAIT, words * 8);
	psram_tx[1] = ((unsigned int)PSRAM_CMD_QREAD << 24) | addr;
	for (tries = 0; tries <= PSRAM_RETRIES; tries++) {
		err = psram_frame(psram_tx, 2, psram_rx, words);
		if (err != PSRAM_E_STALL)
			break;
	}
	if (err != 0)
		return err;
	for (i = 0; i < n; i++)
		dst[i] = (unsigned char)(psram_rx[i >> 2] >> (24 - 8 * (i & 3)));
	return 0;
}

static int
psram_write_chunk(unsigned int addr, const unsigned char *src, unsigned int n)
{
	unsigned int words = (n + 3) / 4, i, w;

	psram_tx[0] = PSRAM_CTRL_WORD(8 + 2 * n, 0, 0);
	psram_tx[1] = ((unsigned int)PSRAM_CMD_QWRITE << 24) | addr;
	for (w = 0; w < words; w++)
		psram_tx[2 + w] = 0;
	for (i = 0; i < n; i++)
		psram_tx[2 + (i >> 2)] |=
		    (unsigned int)src[i] << (24 - 8 * (i & 3));
	return psram_frame(psram_tx, 2 + words, psram_rx, 0);
}

/*
 * Byte offset of a request for len bytes starting at block blkno of bsize
 * bytes. The block number is bounded in blocks before it is multiplied, so
 * the product cannot wrap the 32-bit offset onto a valid address, and the
 * length is bounded against what remains of the device.
 */
int
psram_block_offset(long blkno, unsigned int bsize, unsigned int len,
    unsigned int *off)
{
	unsigned int addr;

	if (blkno < 0 || (unsigned long)blkno > PSRAM_BYTES / bsize)
		return PSRAM_E_RANGE;
	addr = (unsigned int)blkno * bsize;
	if (len > PSRAM_BYTES - addr)
		return PSRAM_E_RANGE;
	*off = addr;
	return 0;
}

int
psram_read(unsigned int addr, unsigned char *dst, unsigned int len)
{
	unsigned int n;
	int err;

	if (addr > PSRAM_BYTES || len > PSRAM_BYTES - addr)
		return PSRAM_E_RANGE;
	while (len != 0) {
		n = psram_chunk_len(addr, len);
		err = psram_read_chunk(addr, dst, n);
		if (err != 0)
			return err;
		addr += n;
		dst += n;
		len -= n;
	}
	return 0;
}

int
psram_write(unsigned int addr, const unsigned char *src, unsigned int len)
{
	unsigned int n;
	int err;

	if (addr > PSRAM_BYTES || len > PSRAM_BYTES - addr)
		return PSRAM_E_RANGE;
	while (len != 0) {
		n = psram_chunk_len(addr, len);
		err = psram_write_chunk(addr, src, n);
		if (err != 0)
			return err;
		addr += n;
		src += n;
		len -= n;
	}
	return 0;
}

/*
 * Bring the device to quad mode from any starting mode. The reset pair
 * goes out in quad format and in serial format: whichever mode the device
 * is in completes one pair, and a frame in the other format is a command
 * cut short that the device does not execute. Then the identification
 * must read manufacturer 0x0d and a passing known-good die, and a pattern
 * written across a 64-byte frame boundary and a page boundary must read
 * back through quad mode. The APS6404L sheet (section 12) admits Read ID
 * only right after the reset, so a Read ID whose eight-word reply overran
 * the RX FIFO repeats the reset with it.
 */
int
psram_device_init(void)
{
	unsigned char pat[PSRAM_SELFTEST], back[PSRAM_SELFTEST];
	unsigned int i, tries;
	int err;

	psram_id_read = 0;
	for (tries = 0; tries <= PSRAM_RETRIES; tries++) {
		PSRAM_DELAY_US(PSRAM_RECOVERY_US);
		if ((err = psram_cmd_quad(PSRAM_CMD_RSTEN)) != 0 ||
		    (err = psram_cmd_quad(PSRAM_CMD_RST)) != 0 ||
		    (err = psram_cmd_serial(PSRAM_CMD_RSTEN)) != 0 ||
		    (err = psram_cmd_serial(PSRAM_CMD_RST)) != 0)
			return err;
		PSRAM_DELAY_US(PSRAM_RECOVERY_US);
		if ((err = psram_read_id()) != PSRAM_E_STALL)
			break;
	}
	if (err != 0)
		return err;
	if (psram_mfid != PSRAM_MFID || psram_kgd != PSRAM_KGD_PASS)
		return PSRAM_E_ID;
	if ((err = psram_cmd_serial(PSRAM_CMD_QUADEN)) != 0)
		return err;

	for (i = 0; i < PSRAM_SELFTEST; i++)
		pat[i] = i < 8 ? (unsigned char)(1u << i) :
		    i < 16 ? (unsigned char)~(1u << (i - 8)) :
		    (unsigned char)(i * 37u + 11u);
	if ((err = psram_write(0x3f0, pat, PSRAM_SELFTEST)) != 0 ||
	    (err = psram_read(0x3f0, back, PSRAM_SELFTEST)) != 0)
		return err;
	for (i = 0; i < PSRAM_SELFTEST; i++)
		if (back[i] != pat[i])
			return PSRAM_E_QUAD;
	return 0;
}
