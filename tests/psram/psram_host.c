/*
 * Forwards the hooks psram_host.h declares to functions registered by the
 * Python harness, and exports the driver entry points it calls.
 */
#include "psram_host.h"
#include <rp2040/dev/psram_pio.h>
#include <rp2040/dev/psram_xfer.h>

typedef unsigned int (*rd_fn)(unsigned int);
typedef void (*wr_fn)(unsigned int, unsigned int);
typedef void (*void_fn)(void);
typedef void (*pin_fn)(unsigned int, unsigned int);
typedef void (*delay_fn)(unsigned int);
typedef int (*save_fn)(void);
typedef void (*restore_fn)(int);

static rd_fn hook_rd;
static wr_fn hook_wr;
static void_fn hook_unreset;
static pin_fn hook_pin;
static delay_fn hook_delay;
static save_fn hook_save;
static restore_fn hook_restore;

void
psram_host_set_hooks(rd_fn rd, wr_fn wr, void_fn unreset, pin_fn pin,
    delay_fn delay, save_fn save, restore_fn restore)
{
	hook_rd = rd;
	hook_wr = wr;
	hook_unreset = unreset;
	hook_pin = pin;
	hook_delay = delay;
	hook_save = save;
	hook_restore = restore;
}

unsigned int
psram_host_rd(unsigned int off)
{
	return hook_rd(off);
}

void
psram_host_wr(unsigned int off, unsigned int v)
{
	hook_wr(off, v);
}

/*
 * PADS_BANK0 GPIO0-GPIO29 at 0x4001c004 + 4n and IO_BANK0 GPIOn_CTRL at
 * 0x40014004 + 8n. A pad register resets to 0x56 (RP2040 datasheet,
 * 2.19.6.3: IE 1, DRIVE 1, PDE 1, SCHMITT 1, the rest 0).
 */
#define	PAD_RESET		0x56u
#define	NPAD			30
static unsigned int pads[NPAD];
static unsigned int ioctrl[NPAD];
static unsigned int scratch;

unsigned int *
psram_host_reg(unsigned long addr)
{
	if (addr >= 0x4001c004ul && addr < 0x4001c004ul + 4 * NPAD)
		return &pads[(addr - 0x4001c004ul) / 4];
	if (addr >= 0x40014004ul && addr < 0x40014004ul + 8 * NPAD &&
	    (addr - 0x40014004ul) % 8 == 0)
		return &ioctrl[(addr - 0x40014004ul) / 8];
	scratch = 0;
	return &scratch;
}

void
psram_host_unreset(void)
{
	unsigned int i;

	for (i = 0; i < NPAD; i++) {
		pads[i] = PAD_RESET;
		ioctrl[i] = 0x1f;
	}
	hook_unreset();
}

unsigned int
psram_host_pad_word(unsigned int n)
{
	return pads[n];
}

void
psram_host_pin(unsigned int n, unsigned int pad)
{
	pc_pin(n, PSRAM_FUNC_PIO0, pad);
	hook_pin(n, pads[n]);
}

void
psram_host_delay_us(unsigned int us)
{
	hook_delay(us);
}

int
psram_host_irq_save(void)
{
	return hook_save();
}

void
psram_host_irq_restore(int s)
{
	hook_restore(s);
}

/* The configuration words and control-word packer, so the test can check
 * the header's expressions against an independent computation. */
unsigned int
psram_host_pinctrl(void)
{
	return PSRAM_PINCTRL;
}

unsigned int
psram_host_pinctrl_csck(void)
{
	return PSRAM_PINCTRL_CSCK;
}

unsigned int
psram_host_execctrl(void)
{
	return PSRAM_EXECCTRL;
}

unsigned int
psram_host_shiftctrl(void)
{
	return PSRAM_SHIFTCTRL;
}

unsigned int
psram_host_clkdiv(void)
{
	return PSRAM_CLKDIV;
}

unsigned int
psram_host_ctrl_word(unsigned int nout, unsigned int wait, unsigned int nin)
{
	return PSRAM_CTRL_WORD(nout, wait, nin);
}

unsigned int
psram_host_chunk(void)
{
	return PSRAM_CHUNK;
}
