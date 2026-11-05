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
 * The PicoCalc keyboard controller, polled over I2C1.
 *
 * Reading a register is a one-byte write of its number followed by a
 * two-byte read; the controller's receive handler prepares the reply when
 * the write lands. FIFO register 0x09 yields one key event per exchange
 * as (state, key), and (0, 0) when the FIFO is empty. The board routes no
 * interrupt from the controller, so a callout polls.
 *
 * The exchange is split across two callouts so that no poll waits on the
 * bus: one queues the write with STOP, the next, KBD_POLL_TICKS later,
 * queues two reads with STOP, and the one after that collects the bytes
 * and queues the next write. DW_apb_i2c (datasheet 4.3) holds the queued
 * commands in its transmit FIFO and runs them on its own. The 16 ms gap
 * between write and read is the wait the vendor's reference driver
 * (picocalc_helloworld/i2ckbd.c) leaves; its FUZIX port splits the same
 * way. A NACK or lost arbitration raises TX_ABRT, which flushes the
 * queued commands; the poll counts it and restarts the exchange.
 *
 * The controller is a DW_apb_i2c master in fast-mode timing at
 * KBD_I2C_HZ. ic_clk is clk_sys (datasheet 4.3.2), and the counts follow
 * the Pico SDK's i2c_set_baudrate: a 3:2 low-to-high split of the SCL
 * period, a spike filter of one sixteenth of the low count, and a 300 ns
 * SDA hold.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>

#include <rp2040/dev/picocalc.h>
#include <rp2040/dev/picocalc_reg.h>
#include <rp2040/dev/picokbd_map.h>
#include <rp2040/dev/lcdterm.h>

#define	I2C1_BASE		0x40048000UL
#define	IC_CON			0x00
#define	IC_TAR			0x04
#define	IC_DATA_CMD		0x10
#define	IC_FS_SCL_HCNT		0x1c
#define	IC_FS_SCL_LCNT		0x20
#define	IC_INTR_MASK		0x30
#define	IC_RAW_INTR_STAT	0x34
#define	IC_RX_TL		0x38
#define	IC_TX_TL		0x3c
#define	IC_CLR_TX_ABRT		0x54
#define	IC_CLR_STOP_DET		0x60
#define	IC_ENABLE		0x6c
#define	IC_RXFLR		0x78
#define	IC_SDA_HOLD		0x7c
#define	IC_ENABLE_STATUS	0x9c
#define	IC_FS_SPKLEN		0xa0

#define	IC_CON_MASTER		(1UL << 0)
#define	IC_CON_SPEED_FAST	(2UL << 1)
#define	IC_CON_RESTART_EN	(1UL << 5)
#define	IC_CON_SLAVE_DISABLE	(1UL << 6)
#define	IC_CON_TX_EMPTY_CTRL	(1UL << 8)

#define	IC_CMD_READ		(1UL << 8)
#define	IC_CMD_STOP		(1UL << 9)

#define	IC_INTR_TX_ABRT		(1UL << 6)
#define	IC_INTR_STOP_DET	(1UL << 9)

#define	I2C_REG(off)		PC_REG32(I2C1_BASE + (off))

#define	KBD_I2C_HZ		100000
#define	KBD_REG_VER		0x01
#define	KBD_REG_FIFO		0x09

#define	KBD_POLL_MS		16
#define	KBD_POLL_TICKS		((KBD_POLL_MS * HZ + 999) / 1000)
#define	KBD_STALL_POLLS		8	/* Phases without progress before reset. */
#define	KBD_SYNC_TIMEOUT_US	50000

#define	KBD_RING		32	/* Power of two. */

enum {
	PH_WRITE,		/* Next: queue the register write. */
	PH_READ,		/* Write queued; next: queue the reads. */
	PH_COLLECT,		/* Reads queued; next: take two bytes. */
};

static int phase;
static int stall;
static int ctrl;
static int polling;
static int fw_version = -1;
static u_int kbd_errors;

static char ring[KBD_RING];
static u_int ring_head, ring_tail;

static void
i2c_enable(int on)
{
	u_int t0 = pc_micros();

	I2C_REG(IC_ENABLE) = on ? 1 : 0;
	while ((I2C_REG(IC_ENABLE_STATUS) & 1) != (u_int)(on ? 1 : 0) &&
	    pc_micros() - t0 < 10000)
		continue;
}

static void
i2c_restart(void)
{
	i2c_enable(0);
	(void)I2C_REG(IC_CLR_TX_ABRT);
	(void)I2C_REG(IC_CLR_STOP_DET);
	i2c_enable(1);
	phase = PH_WRITE;
	stall = 0;
}

void
picokbd_init(void)
{
	u_int period, lcnt, hcnt;

	pc_unreset(PC_RESET_IO_BANK0 | PC_RESET_PADS_BANK0 | PC_RESET_I2C1);

	i2c_enable(0);
	I2C_REG(IC_CON) = IC_CON_MASTER | IC_CON_SPEED_FAST |
	    IC_CON_RESTART_EN | IC_CON_SLAVE_DISABLE | IC_CON_TX_EMPTY_CTRL;
	I2C_REG(IC_TX_TL) = 0;
	I2C_REG(IC_RX_TL) = 0;
	I2C_REG(IC_INTR_MASK) = 0;

	period = (CPU_KHZ * 1000UL + KBD_I2C_HZ / 2) / KBD_I2C_HZ;
	lcnt = period * 3 / 5;
	hcnt = period - lcnt;
	I2C_REG(IC_FS_SCL_HCNT) = hcnt;
	I2C_REG(IC_FS_SCL_LCNT) = lcnt;
	I2C_REG(IC_FS_SPKLEN) = lcnt < 16 ? 1 : lcnt / 16;
	I2C_REG(IC_SDA_HOLD) = (CPU_KHZ * 3UL) / 10000 + 1;
	I2C_REG(IC_TAR) = PICOCALC_KBD_ADDR;

	pc_pin(PICOCALC_KBD_SDA, PC_FUNC_I2C, PC_PADS_PUE | PC_PADS_SCHMITT);
	pc_pin(PICOCALC_KBD_SCL, PC_FUNC_I2C, PC_PADS_PUE | PC_PADS_SCHMITT);

	i2c_restart();
}

#define	STEP_WAIT	(-1)	/* The bus has not finished the phase. */
#define	STEP_DONE	0	/* Phase advanced; no key event. */
#define	STEP_EVENT	1	/* Collected a non-empty event. */

/*
 * Advance the exchange of register reg by one phase. An abort restarts
 * the controller and reports STEP_DONE with phase back at PH_WRITE.
 */
static int
kbd_step(int reg, int *state, int *key)
{
	u_int raw = I2C_REG(IC_RAW_INTR_STAT);

	if (raw & IC_INTR_TX_ABRT) {
		kbd_errors++;
		i2c_restart();
		return STEP_DONE;
	}

	switch (phase) {
	case PH_WRITE:
		(void)I2C_REG(IC_CLR_STOP_DET);
		I2C_REG(IC_DATA_CMD) = (u_int)reg | IC_CMD_STOP;
		phase = PH_READ;
		return STEP_DONE;
	case PH_READ:
		if ((raw & IC_INTR_STOP_DET) == 0)
			return STEP_WAIT;
		(void)I2C_REG(IC_CLR_STOP_DET);
		I2C_REG(IC_DATA_CMD) = IC_CMD_READ;
		I2C_REG(IC_DATA_CMD) = IC_CMD_READ | IC_CMD_STOP;
		phase = PH_COLLECT;
		return STEP_DONE;
	default:
		if (I2C_REG(IC_RXFLR) < 2)
			return STEP_WAIT;
		*state = (int)(I2C_REG(IC_DATA_CMD) & 0xff);
		*key = (int)(I2C_REG(IC_DATA_CMD) & 0xff);
		phase = PH_WRITE;
		return (*state != 0 || *key != 0) ? STEP_EVENT : STEP_DONE;
	}
}

static void
ring_put(const char *s, int n)
{
	while (n-- > 0) {
		if (ring_head - ring_tail >= KBD_RING)
			return;
		ring[ring_head++ % KBD_RING] = *s++;
	}
}

static void
kbd_poll(caddr_t arg __unused)
{
	char seq[PICOKBD_MAXSEQ];
	int state, key, n, r;

	r = kbd_step(KBD_REG_FIFO, &state, &key);
	if (r == STEP_WAIT) {
		if (++stall > KBD_STALL_POLLS) {
			kbd_errors++;
			i2c_restart();
		}
	} else {
		stall = 0;
		if (r == STEP_EVENT) {
			n = picokbd_translate(state, key, lcdterm_app_cursor(),
			    &ctrl, seq);
			if (n > 0)
				lcdcons_input(seq, n);
		}
		/* A collected exchange queues the next write at once. */
		if (phase == PH_WRITE)
			(void)kbd_step(KBD_REG_FIFO, &state, &key);
	}
	timeout(kbd_poll, (caddr_t)0, KBD_POLL_TICKS);
}

/*
 * Start polling. timeout() needs the callout list that main() builds after
 * config() probes devices, so the console's first open starts the poll.
 */
void
picokbd_poll_start(void)
{
	int s = spltty();

	if (!polling) {
		polling = 1;
		timeout(kbd_poll, (caddr_t)0, KBD_POLL_TICKS);
	}
	splx(s);
}

/* Step until the phase completes; STEP_WAIT on a timeout. */
static int
kbd_step_sync(int reg, int *state, int *key)
{
	u_int t0 = pc_micros();
	int r;

	while ((r = kbd_step(reg, state, key)) == STEP_WAIT) {
		if (pc_micros() - t0 >= KBD_SYNC_TIMEOUT_US) {
			kbd_errors++;
			i2c_restart();
			break;
		}
	}
	return r;
}

/*
 * Run one whole exchange of register reg by busy waiting, KBD_POLL_MS
 * between write and read, with interrupts masked so that the poll callout
 * cannot advance the same exchange. Used at probe time, before callouts
 * run, and by the console's polled getc. Returns 1 with a non-empty
 * reply, 0 for an empty one, a timeout or an abort.
 */
static int
kbd_exchange(int reg, int *state, int *key)
{
	u_int t0;
	int s, r = STEP_DONE;

	s = spltty();
	*state = *key = 0;
	if (phase != PH_WRITE)
		i2c_restart();
	(void)kbd_step(reg, state, key);
	t0 = pc_micros();
	while (pc_micros() - t0 < KBD_POLL_MS * 1000)
		continue;
	if (kbd_step_sync(reg, state, key) == STEP_DONE && phase == PH_COLLECT)
		r = kbd_step_sync(reg, state, key);
	splx(s);
	return r == STEP_EVENT;
}

int
picokbd_version(void)
{
	int state, key;

	if (fw_version < 0 && kbd_exchange(KBD_REG_VER, &state, &key))
		fw_version = key;
	return fw_version;
}

/* Polled read of one byte for the console's getc; waits for a key. */
int
picokbd_getc(void)
{
	char seq[PICOKBD_MAXSEQ];
	int state, key, n;

	while (ring_head == ring_tail) {
		if (kbd_exchange(KBD_REG_FIFO, &state, &key)) {
			n = picokbd_translate(state, key, lcdterm_app_cursor(),
			    &ctrl, seq);
			ring_put(seq, n);
		}
	}
	return (u_char)ring[ring_tail++ % KBD_RING];
}

u_int
picokbd_errors(void)
{
	return kbd_errors;
}
