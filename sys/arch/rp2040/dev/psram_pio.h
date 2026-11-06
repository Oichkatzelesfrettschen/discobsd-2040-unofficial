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
 * The PIO program that clocks the PicoCalc's ESP-PSRAM64H, and the
 * register values that bind it to the board's pins.
 *
 * One transaction is one chip-select frame. The state machine takes a
 * control word from the TX FIFO, shifts a count of nibbles out on
 * SIO0-SIO3, releases the pins, clocks a count of turnaround cycles and
 * shifts a count of nibbles in. A serial-mode command is sent one bit per
 * nibble on SIO0 and read back from SIO1, so the same program serves the
 * single-bit commands before quad mode and every quad transfer after it.
 * Documented in docs/research/picocalc-psram.md; tests/psram holds the
 * simulator that executes these words against a model of the device.
 *
 * Pin mapping (PicoCalc.md "Pin map"): SIO0-SIO3 are GP2-GP5, the OUT, IN
 * and SET base; chip select GP20 is side-set bit 0 and SCK GP21 is side-set
 * bit 1, two non-optional side-set bits that leave three delay bits.
 *
 * Instruction encoding, RP2040 datasheet 3.4.1: bits 15-13 the opcode,
 * 12-11 the side-set value, 10-8 the delay, 7-0 the operand. The side-set
 * values are IDLE (CS high, SCK low) 1, LOW (CS low, SCK low) 0 and HIGH
 * (CS low, SCK high) 2.
 *
 * Control word, shifted out of the OSR from the most significant bit (the
 * OUT shift direction is left):
 *   31..22  nibbles to send, less one          -> X
 *   21..17  turnaround pulses plus 3, less one -> Y
 *   16..7   nibbles to read                    -> ISR, then X
 *    6..0   unused, discarded
 * Data nibbles follow as 32-bit words, first nibble in bits 31..28.
 * The read side autopushes a word per eight nibbles, so a read count is a
 * multiple of eight.
 *
 * The read path samples two pulses behind the rising edge that launched
 * the nibble: the IN instruction sits on the falling-edge cycle of the
 * pass after a rising edge, three PIO cycles behind the pad because the
 * input synchronizers are engaged (RP2040 datasheet 3.5.6.3).
 */

#ifndef _RP2040_DEV_PSRAM_PIO_H_
#define _RP2040_DEV_PSRAM_PIO_H_

#define	PSRAM_PIO_LENGTH	19

/*
 * tests/psram/pio_sim.py parses PSRAM_PIO_WORDS, checks every word against
 * the assembly in its comment and executes it. Each entry is one line:
 * the word, then a comment holding the address, the assembly, the side-set
 * value and a delay when it is nonzero.
 */
#define	PSRAM_PIO_WORDS \
	0x88a0,	/* 00: pull block              side 1 */ \
	0x682a,	/* 01: out x, 10               side 1 */ \
	0x6845,	/* 02: out y, 5                side 1 */ \
	0x68ca,	/* 03: out isr, 10             side 1 */ \
	0x6867,	/* 04: out null, 7             side 1 */ \
	0xe88f,	/* 05: set pindirs, 15         side 1 */ \
	0x6004,	/* 06: out pins, 4             side 0 */ \
	0x1046,	/* 07: jmp x--, 6              side 2 */ \
	0xe080,	/* 08: set pindirs, 0          side 0 */ \
	0xa026,	/* 09: mov x, isr              side 0 */ \
	0xa0c3,	/* 10: mov isr, null           side 0 */ \
	0x0031,	/* 11: jmp !x, 17              side 0 */ \
	0x004d,	/* 12: jmp x--, 13             side 0 */ \
	0xa042,	/* 13: nop                      side 0 */ \
	0x108d,	/* 14: jmp y--, 13             side 2 */ \
	0x4004,	/* 15: in pins, 4              side 0 */ \
	0x104f,	/* 16: jmp x--, 15             side 2 */ \
	0xa142,	/* 17: nop                      side 0 [1] */ \
	0x0800	/* 18: jmp 0                   side 1 */

/* Pins, RP2040 datasheet 2.19 numbering. */
#define	PSRAM_PIN_SIO0		2
#define	PSRAM_PIN_CS		20
#define	PSRAM_PIN_SCK		21

/* PIO block 0, datasheet 3.7. State machine 0 runs the program. */
#define	PSRAM_PIO_BASE		0x50200000UL
#define	PSRAM_PIO_CTRL		0x000
#define	PSRAM_PIO_FSTAT		0x004
#define	PSRAM_PIO_FDEBUG	0x008
#define	PSRAM_PIO_TXF0		0x010
#define	PSRAM_PIO_RXF0		0x020
#define	PSRAM_PIO_INSTR_MEM	0x048
#define	PSRAM_PIO_SM_CLKDIV	0x0c8
#define	PSRAM_PIO_SM_EXECCTRL	0x0cc
#define	PSRAM_PIO_SM_SHIFTCTRL	0x0d0
#define	PSRAM_PIO_SM_ADDR	0x0d4
#define	PSRAM_PIO_SM_INSTR	0x0d8
#define	PSRAM_PIO_SM_PINCTRL	0x0dc

#define	PSRAM_FSTAT_RXEMPTY	(1UL << 8)
#define	PSRAM_FSTAT_TXFULL	(1UL << 16)
#define	PSRAM_FSTAT_TXEMPTY	(1UL << 24)
#define	PSRAM_FDEBUG_RXSTALL	(1UL << 0)
#define	PSRAM_SHIFTCTRL_FJOIN_TX	(1UL << 30)

/*
 * Wrap the whole program, autopull and autopush at 32 bits, shift left in
 * both directions.
 */
#define	PSRAM_EXECCTRL	((PSRAM_PIO_LENGTH - 1) << 12)
#define	PSRAM_SHIFTCTRL	((1UL << 17) | (1UL << 16))
#define	PSRAM_PINCTRL	((2UL << 29) | (4UL << 26) | (4UL << 20) | \
			    ((unsigned int)PSRAM_PIN_SIO0 << 15) | \
			    ((unsigned int)PSRAM_PIN_CS << 10) | \
			    ((unsigned int)PSRAM_PIN_SIO0 << 5) | PSRAM_PIN_SIO0)

/* Instructions written to SM_INSTR to set up the chip-select and clock pins. */
#define	PSRAM_PINCTRL_CSCK	((2UL << 26) | ((unsigned int)PSRAM_PIN_CS << 5))
#define	PSRAM_INSTR_SET_PINS_IDLE	0xe001	/* set pins, 1: CS high, SCK low */
#define	PSRAM_INSTR_SET_PINDIRS_CSCK	0xe083	/* set pindirs, 3 */
#define	PSRAM_INSTR_JMP_IDLE		0x0800	/* jmp 0 side 1 */

/* PIO cycles per system clock cycle: SCK runs at clk_sys / (2 * div). */
#define	PSRAM_CLKDIV		2

#define	PSRAM_CTRL_WORD(nout, wait, nin) \
	((((unsigned int)(nout) - 1) << 22) | (((unsigned int)(wait) + 2) << 17) | \
	    ((unsigned int)(nin) << 7))

#endif /* !_RP2040_DEV_PSRAM_PIO_H_ */
