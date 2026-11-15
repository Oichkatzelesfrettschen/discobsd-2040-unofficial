/*
 * Host gate for the USB console's transmit state across bus events.
 *
 * The gate links the production driver, sys/arch/rp2040/dev/usb.c, through
 * usb_console_extract.py, which routes its register macros to the model
 * below, together with sys/kern/tty.c and sys/kern/tty_subr.c compiled
 * unchanged. A modeled host drives the controller: control requests on EP0,
 * bulk IN packets taken with or without a completion, bulk OUT keystrokes,
 * bus reset. Modeled processes write to the tty and close it on their own
 * stacks (usb_console_sched.c), so ttwrite() and ttywait() sleep and resume
 * the way they do on the board.
 *
 * The property: tty output has a wakeup source whenever the host reads.
 * After any event sequence, a host that opens the port, reads every packet
 * the device arms and types a carriage return must see the echo, and no
 * process may stay asleep on the output queue. A driver that leaves TS_BUSY
 * set with no bulk IN packet outstanding fails it: usbstart() returns early
 * on TS_BUSY, and only a bulk IN completion clears it.
 *
 * With no argument the gate replays the fixed sequences below and exits 1 on
 * a violation. "--search DEPTH" enumerates every sequence of the event
 * alphabet up to DEPTH and prints the shortest sequence reaching each class
 * of stuck state; "--random SEED COUNT LENGTH" samples longer sequences.
 */
#include "hostkern.h"
#include "usb_console_sched.h"

/*
 * The port's arm_dsb() is a DSB instruction, which only the driver's
 * reset-to-flash path issues; the gate never takes that path. The header is
 * included first so the driver's own include is a no-op and the name can be
 * routed to the model's settle hook.
 */
#include <sys/types.h>
#include <machine/scb.h>
void usb_host_settle(void);
#define	arm_dsb()	usb_host_settle()

#ifndef USB_CONSOLE_DRIVER
#define	USB_CONSOLE_DRIVER	"usb_console_driver.c"
#endif
#include USB_CONSOLE_DRIVER

#include <sys/clist.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/dk.h>

/* ------------------------------------------------------------------ */
/* Kernel entry points tty.c and the driver call.                      */

struct user u;
int hz = 100;
int lbolt;
long tk_nin;
long tk_nout;
int selwait;
static struct proc hk_proc;

_Alignas(CBLOCK) struct cblock cfree[NCLIST];

int nullstop(struct tty *tp, int flag);

const struct cdevsw cdevsw[UARTUSB_MAJOR + 1] = {
	[UARTUSB_MAJOR] = {
		usbopen, usbclose, usbread, usbwrite,
		usbioctl, nullstop, usbttys, usbselect,
		nostrategy, usbgetc, usbputc,
	},
};

int
nullstop(struct tty *tp, int flag)
{
	(void)tp;
	(void)flag;
	return 0;
}

void
nostrategy(struct buf *bp)
{
	(void)bp;
}

/*
 * usbinit() builds the serial string from the flash identity; the gate
 * never calls usbinit(), so the model reports the identity unread.
 */
int
flash_identity(const unsigned char **jedec, const unsigned char **unique)
{
	(void)jedec;
	(void)unique;
	return -1;
}

/* ttioctl() resolves process groups; the gate issues no ioctl. */
struct proc *
pfind(int pid)
{
	(void)pid;
	return NULL;
}

int
inferior(struct proc *p)
{
	(void)p;
	return 0;
}

void
sleep(caddr_t chan, int pri)
{
	int s = hk_ipl;

	(void)pri;
	/* The scheduler runs the next process at priority zero. */
	hk_ipl = 0;
	ucs_sleep(chan);
	hk_ipl = s;
}

void
wakeup(caddr_t chan)
{
	ucs_wakeup(chan);
}

void
selwakeup(struct proc *p, long coll)
{
	(void)p;
	(void)coll;
}

unsigned hk_signals;

void
gsignal(int pgrp, int sig)
{
	(void)pgrp;
	(void)sig;
	hk_signals++;
}

void
psignal(struct proc *p, int sig)
{
	(void)p;
	(void)sig;
	hk_signals++;
}

void
timeout(void (*fun)(caddr_t), caddr_t arg, int t)
{
	(void)fun;
	(void)arg;
	(void)t;
}

void
untimeout(void (*fun)(caddr_t), caddr_t arg)
{
	(void)fun;
	(void)arg;
}

int
uiomove(caddr_t cp, u_int n, struct uio *uio)
{
	struct iovec *iov = uio->uio_iov;
	u_int i;

	if (n > iov->iov_len)
		n = iov->iov_len;
	for (i = 0; i < n; i++)
		cp[i] = ((char *)iov->iov_base)[i];
	iov->iov_base = (char *)iov->iov_base + n;
	iov->iov_len -= n;
	uio->uio_resid -= n;
	uio->uio_offset += n;
	return 0;
}

int
ureadc(int c, struct uio *uio)
{
	(void)c;
	uio->uio_resid--;
	return 0;
}

void
led_control(int mask, int on)
{
	(void)mask;
	(void)on;
}

void
board_console(int *major_out, int *minor_out)
{
	*major_out = UARTUSB_MAJOR;
	*minor_out = 0;
}

void *
rom_func_lookup(u_int code)
{
	(void)code;
	panic("usb console gate: ROM lookup");
}

/* ------------------------------------------------------------------ */
/* Controller model.                                                   */

#define	MODEL_REGS_WORDS	(0x100 / 4)
#define	ALIAS_NONE	0
#define	ALIAS_SET	1
#define	ALIAS_CLR	2

static u_int model_regs[MODEL_REGS_WORDS];
static union {
	u_int	words[USB_DPRAM_SIZE / 4];
	u_char	bytes[USB_DPRAM_SIZE];
} model_dpram;
static u_int model_alias_value;
static int model_alias_kind;
static u_long model_alias_offset;
static int model_sof_pending;
static u_int model_now;
static u_int model_scratch;

/* Apply a write the driver made through a SET or CLR alias. */
static void
model_fold_alias(void)
{
	u_int *reg;

	if (model_alias_kind == ALIAS_NONE)
		return;
	reg = &model_regs[model_alias_offset / 4];
	if (model_alias_kind == ALIAS_SET)
		*reg |= model_alias_value;
	else
		*reg &= ~model_alias_value;
	model_alias_kind = ALIAS_NONE;
	model_alias_value = 0;
}

static u_int
model_ints(void)
{
	u_int raw = 0;

	if (model_regs[USB_BUFF_STATUS / 4] != 0)
		raw |= USB_INTS_BUFF_STATUS;
	if (model_regs[USB_SIE_STATUS / 4] & USB_SIE_STATUS_BUS_RESET)
		raw |= USB_INTS_BUS_RESET;
	if (model_regs[USB_SIE_STATUS / 4] & USB_SIE_STATUS_SETUP_REC)
		raw |= USB_INTS_SETUP_REQ;
	if (model_sof_pending)
		raw |= USB_INTS_DEV_SOF;
	return raw & model_regs[USB_INTE / 4];
}

volatile u_int *
usb_host_reg32(u_long address)
{
	u_long offset;

	model_fold_alias();
	if (address >= USB_REGS_BASE && address < USB_REGS_BASE + 0x4000) {
		offset = address - USB_REGS_BASE;
		if (offset >= USB_REG_CLR) {
			model_alias_kind = ALIAS_CLR;
			model_alias_offset = offset - USB_REG_CLR;
			return &model_alias_value;
		}
		if (offset >= USB_REG_SET) {
			model_alias_kind = ALIAS_SET;
			model_alias_offset = offset - USB_REG_SET;
			return &model_alias_value;
		}
		if (offset >= 0x100)
			panic("usb console gate: register outside the model");
		if (offset == USB_INTS)
			model_regs[USB_INTS / 4] = model_ints();
		if (offset == USB_SOF_RD)
			model_sof_pending = 0;
		return &model_regs[offset / 4];
	}
	if (address >= USB_DPRAM_BASE &&
	    address < USB_DPRAM_BASE + USB_DPRAM_SIZE)
		return &model_dpram.words[(address - USB_DPRAM_BASE) / 4];
	if (address == TIMER_BASE + TIMER_TIMERAWL) {
		model_scratch = model_now;
		return &model_scratch;
	}
	panic("usb console gate: address outside the model");
}

volatile u_char *
usb_host_dpram8(u_long offset)
{
	model_fold_alias();
	if (offset >= USB_DPRAM_SIZE)
		panic("usb console gate: DPSRAM offset outside the model");
	return &model_dpram.bytes[offset];
}

void
usb_host_settle(void)
{
}

static u_int
model_buf_ctrl(int ep, int in)
{
	model_fold_alias();
	return model_dpram.words[USB_DPRAM_BUF_CTRL(ep, in) / 4];
}

static void
model_set_buf_ctrl(int ep, int in, u_int v)
{
	model_dpram.words[USB_DPRAM_BUF_CTRL(ep, in) / 4] = v;
}

/* The controller raises the interrupt; PRIMASK is clear at every call. */
static void
model_interrupt(void)
{
	model_fold_alias();
	if (hk_ipl != 0)
		panic("usb console gate: interrupt with PRIMASK set");
	if (model_ints() != 0)
		usbintr();
	model_fold_alias();
}

/* ------------------------------------------------------------------ */
/* Host model.                                                         */

static int host_open;			/* A terminal holds the port. */
static int orphan_seen;			/* TS_BUSY with no packet, any time. */
static unsigned host_bytes;		/* Bulk IN bytes the host received. */
static char host_text[512];
static unsigned host_text_len;

static void
host_take_in(int ep, u_int bc)
{
	u_int n = bc & USB_BUF_CTRL_LEN_MASK, i;

	if (ep != EP_DATA)
		return;
	for (i = 0; i < n; i++) {
		if (host_text_len < sizeof(host_text) - 1)
			host_text[host_text_len++] =
			    (char)model_dpram.bytes[BUF_DATA_IN + i];
	}
	host_text[host_text_len] = '\0';
	host_bytes += n;
}

/* Complete whatever EP0 stage the device armed, as a host would. */
static void
host_ep0_finish(void)
{
	int round;
	u_int bc;

	for (round = 0; round < 16; round++) {
		bc = model_buf_ctrl(0, 1);
		if (bc & USB_BUF_CTRL_AVAIL) {
			model_set_buf_ctrl(0, 1,
			    bc & ~(USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_FULL));
			model_regs[USB_BUFF_STATUS / 4] |=
			    USB_BUFF_STATUS_BIT(0, 1);
			model_interrupt();
			continue;
		}
		bc = model_buf_ctrl(0, 0);
		if (usbd.stage == EP0_OUT_STATUS &&
		    (bc & USB_BUF_CTRL_AVAIL)) {
			model_set_buf_ctrl(0, 0,
			    (bc & ~(USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_LEN_MASK)) |
			    USB_BUF_CTRL_FULL);
			model_regs[USB_BUFF_STATUS / 4] |=
			    USB_BUFF_STATUS_BIT(0, 0);
			model_interrupt();
			continue;
		}
		break;
	}
}

static void
host_setup(u_int type, u_int request, u_int value, u_int index, u_int length)
{
	model_dpram.bytes[0] = (u_char)type;
	model_dpram.bytes[1] = (u_char)request;
	model_dpram.bytes[2] = (u_char)value;
	model_dpram.bytes[3] = (u_char)(value >> 8);
	model_dpram.bytes[4] = (u_char)index;
	model_dpram.bytes[5] = (u_char)(index >> 8);
	model_dpram.bytes[6] = (u_char)length;
	model_dpram.bytes[7] = (u_char)(length >> 8);
	model_regs[USB_SIE_STATUS / 4] |= USB_SIE_STATUS_SETUP_REC;
	model_interrupt();
	host_ep0_finish();
}

static void
host_set_configuration(void)
{
	host_setup(0x00, REQ_SET_CONFIGURATION, USB_CONFIGURATION_VALUE, 0, 0);
}

static void
host_set_dtr(int on)
{
	host_setup(0x21, CDC_SET_CONTROL_LINE_STATE, on ? 3 : 0,
	    ITF_CDC_COMM, 0);
}

static void
host_bus_reset(void)
{
	model_regs[USB_SIE_STATUS / 4] |= USB_SIE_STATUS_BUS_RESET;
	model_interrupt();
}

static int
host_in_armed(void)
{
	return (model_buf_ctrl(EP_DATA, 1) & USB_BUF_CTRL_AVAIL) != 0;
}

/* The host reads the armed bulk IN packet and the device sees it complete. */
static void
host_in(void)
{
	u_int bc = model_buf_ctrl(EP_DATA, 1);

	host_take_in(EP_DATA, bc);
	model_set_buf_ctrl(EP_DATA, 1,
	    bc & ~(USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_FULL));
	model_regs[USB_BUFF_STATUS / 4] |= USB_BUFF_STATUS_BIT(EP_DATA, 1);
	model_interrupt();
}

/*
 * The armed bulk IN buffer returns to the processor with no BUFF_STATUS bit:
 * the state usb_tx_recover_if_idle() recognizes. Nothing in the datasheet
 * says the controller produces it; the driver handles it, so the gate does.
 */
static void
host_in_lost(void)
{
	u_int bc = model_buf_ctrl(EP_DATA, 1);

	model_set_buf_ctrl(EP_DATA, 1,
	    bc & ~(USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_FULL));
}

/* One keystroke on the bulk OUT endpoint; returns 0 when the device NAKs. */
static int
host_out(u_char c)
{
	u_int bc = model_buf_ctrl(EP_DATA, 0);

	if ((bc & USB_BUF_CTRL_AVAIL) == 0)
		return 0;
	model_dpram.bytes[BUF_DATA_OUT] = c;
	model_set_buf_ctrl(EP_DATA, 0,
	    (bc & ~(USB_BUF_CTRL_AVAIL | USB_BUF_CTRL_LEN_MASK)) |
	    USB_BUF_CTRL_FULL | 1);
	model_regs[USB_BUFF_STATUS / 4] |= USB_BUFF_STATUS_BIT(EP_DATA, 0);
	model_interrupt();
	return 1;
}

/* An SOF interrupt, which also services the controller. */
static void
host_sof(void)
{
	model_sof_pending = 1;
	model_interrupt();
}

/* ------------------------------------------------------------------ */
/* Modeled processes.                                                  */

#define	PROC_REBOOT	0	/* reboot(8): "CAUTION ..." on stderr. */
#define	PROC_WRITER	1	/* A writer past the high-water mark. */
#define	PROC_CLOSER	2	/* The last close of the console. */

static const char reboot_text[] = "CAUTION: some process(es) wouldn't die\n";
static char writer_text[400];

static void
proc_write(const char *text, u_int len)
{
	struct iovec iov;
	struct uio uio;

	iov.iov_base = (caddr_t)text;
	iov.iov_len = len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = len;
	uio.uio_rw = UIO_WRITE;
	(void)usbwrite(makedev(UARTUSB_MAJOR, 0), &uio, 0);
}

static void
proc_reboot_body(void)
{
	proc_write(reboot_text, sizeof(reboot_text) - 1);
}

static void
proc_writer_body(void)
{
	proc_write(writer_text, sizeof(writer_text));
}

static void
proc_closer_body(void)
{
	(void)usbclose(makedev(UARTUSB_MAJOR, 0), 0, 0);
}

static void
run_processes(void)
{
	hk_ipl = 0;
	(void)ucs_run();
	hk_ipl = 0;
}

/* ------------------------------------------------------------------ */
/* Events.                                                             */

enum event {
	EV_OPEN,	/* Terminal opens the port: DTR on. */
	EV_CLOSE,	/* Terminal closes the port: DTR off. */
	EV_SETCONF,	/* SET_CONFIGURATION with no bus reset. */
	EV_RESET,	/* Bus reset, then SET_CONFIGURATION and DTR. */
	EV_SETIF,	/* SET_INTERFACE on the data interface. */
	EV_CLRHALT,	/* CLEAR_FEATURE(ENDPOINT_HALT) on bulk IN. */
	EV_IN,		/* Host reads the armed bulk IN packet. */
	EV_INLOST,	/* Armed bulk IN returns with no completion. */
	EV_SOF,		/* A start of frame. */
	EV_TYPE,	/* Host types a printable byte. */
	EV_CR,		/* Host types a carriage return. */
	EV_REBOOT,	/* reboot(8) writes its warning. */
	EV_WRITER,	/* A process writes past the high-water mark. */
	EV_CLOSER,	/* A process makes the last close. */
	EV_COUNT
};

static const char *const event_names[EV_COUNT] = {
	"open", "close", "setconf", "reset", "setif", "clrhalt", "in",
	"inlost", "sof", "type", "cr", "reboot-write", "writer", "last-close",
};

#define	MAX_DEPTH	12
#define	MAX_SEQUENCE	48

/* Events the observation's host could have produced, by the driver call. */
static const char event_host_ok[EV_COUNT] = {
	1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static int
event_enabled(enum event e)
{
	struct tty *tp = &usbttys[0];

	switch (e) {
	case EV_OPEN:
		return !host_open;
	case EV_CLOSE:
		return host_open;
	case EV_IN:
		return host_open && host_in_armed();
	case EV_INLOST:
		return host_in_armed();
	case EV_TYPE:
	case EV_CR:
		return host_open;
	case EV_REBOOT:
		return ucs_state(PROC_REBOOT) == UCS_IDLE &&
		    (tp->t_state & TS_ISOPEN);
	case EV_WRITER:
		return ucs_state(PROC_WRITER) == UCS_IDLE &&
		    (tp->t_state & TS_ISOPEN);
	case EV_CLOSER:
		/* The last close: no other process holds the console. */
		return ucs_state(PROC_CLOSER) == UCS_IDLE &&
		    (tp->t_state & TS_ISOPEN) &&
		    ucs_state(PROC_REBOOT) != UCS_ASLEEP &&
		    ucs_state(PROC_WRITER) != UCS_ASLEEP;
	default:
		return 1;
	}
}

static void
event_apply(enum event e)
{
	switch (e) {
	case EV_OPEN:
		host_open = 1;
		host_set_dtr(1);
		break;
	case EV_CLOSE:
		host_open = 0;
		host_set_dtr(0);
		break;
	case EV_SETCONF:
		host_set_configuration();
		break;
	case EV_RESET:
		host_bus_reset();
		host_set_configuration();
		if (host_open)
			host_set_dtr(1);
		break;
	case EV_SETIF:
		host_setup(0x01, REQ_SET_INTERFACE, 0, ITF_CDC_DATA, 0);
		break;
	case EV_CLRHALT:
		host_setup(0x02, REQ_CLEAR_FEATURE, 0, 0x82, 0);
		break;
	case EV_IN:
		host_in();
		break;
	case EV_INLOST:
		host_in_lost();
		break;
	case EV_SOF:
		host_sof();
		break;
	case EV_TYPE:
		(void)host_out('x');
		break;
	case EV_CR:
		(void)host_out('\r');
		break;
	case EV_REBOOT:
		ucs_start(PROC_REBOOT, proc_reboot_body);
		break;
	case EV_WRITER:
		ucs_start(PROC_WRITER, proc_writer_body);
		break;
	case EV_CLOSER:
		ucs_start(PROC_CLOSER, proc_closer_body);
		break;
	case EV_COUNT:
		break;
	}
	model_now += 1000;
	run_processes();
	if ((usbttys[0].t_state & TS_BUSY) && !usbd.tx_busy && !usbd.tx_pending)
		orphan_seen = 1;
}

/* ------------------------------------------------------------------ */
/* Scenario setup and the verdict.                                     */

static void
clist_reset(void)
{
	int i, j;

	for (i = 0; i < NCLIST; i++) {
		cfree[i].c_next = NULL;
		for (j = 0; j < (int)CBSIZE; j++)
			cfree[i].c_info[j] = 0;
	}
	cfreelist = NULL;
	cfreecount = 0;
	for (i = NCLIST; i-- > 0; ) {
		cfree[i].c_next = cfreelist;
		cfreelist = &cfree[i];
		cfreecount += CBSIZE;
	}
}

static void
drain_in(void)
{
	int round;

	for (round = 0; round < 512 && host_open && host_in_armed(); round++) {
		host_in();
		run_processes();
	}
}

/*
 * The state the reboot path starts from: enumerated, a terminal holding the
 * port, rc's output on the console and drained.
 */
static void
scenario_start(void)
{
	static const char rc_text[] = "--- Errors repaired, rebooting...\n";
	int i;

	for (i = 0; i < MODEL_REGS_WORDS; i++)
		model_regs[i] = 0;
	for (i = 0; i < (int)(USB_DPRAM_SIZE / 4); i++)
		model_dpram.words[i] = 0;
	model_alias_kind = ALIAS_NONE;
	model_sof_pending = 0;
	model_now = 1000000;
	bzero(&usbd, sizeof(usbd));
	usb_service_depth = 0;
	usb_sof_us = 0;
	usb_tx_recovered = 0;
	bzero(usbttys, sizeof(usbttys));
	clist_reset();
	ucs_reset();
	hk_ipl = 0;
	host_open = 0;
	orphan_seen = 0;
	host_bytes = 0;
	host_text_len = 0;
	host_text[0] = '\0';
	for (i = 0; i < (int)sizeof(writer_text); i++)
		writer_text[i] = (i % 64 == 63) ? '\n' : 'w';

	bzero(&hk_proc, sizeof(hk_proc));
	hk_proc.p_pid = 2;
	u.u_procp = &hk_proc;

	model_regs[USB_INTE / 4] = USB_INTS_BUFF_STATUS | USB_INTS_BUS_RESET |
	    USB_INTS_SETUP_REQ;
	DPRAM32(USB_DPRAM_EP_CTRL(EP_DATA, 0)) = USB_EP_CTRL_ENABLE;
	DPRAM32(USB_DPRAM_EP_CTRL(EP_DATA, 1)) = USB_EP_CTRL_ENABLE;
	host_set_configuration();
	host_open = 1;
	host_set_dtr(1);
	(void)usbopen(makedev(UARTUSB_MAJOR, 0), 0, 0);
	usbttys[0].t_pgrp = hk_proc.p_pid;
	hk_proc.p_pgrp = hk_proc.p_pid;
	{
		struct iovec iov;
		struct uio uio;

		iov.iov_base = (caddr_t)rc_text;
		iov.iov_len = sizeof(rc_text) - 1;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_offset = 0;
		uio.uio_resid = sizeof(rc_text) - 1;
		uio.uio_rw = UIO_WRITE;
		(void)usbwrite(makedev(UARTUSB_MAJOR, 0), &uio, 0);
	}
	drain_in();
	host_bytes = 0;
	host_text_len = 0;
	host_text[0] = '\0';
}

#define	STUCK_ECHO	0x01	/* A typed CR produced no output. */
#define	STUCK_REBOOT	0x02	/* reboot(8) asleep in ttwrite. */
#define	STUCK_WRITER	0x04	/* The writer asleep in ttwrite. */
#define	STUCK_CLOSER	0x08	/* The last close asleep in ttywait. */
#define	STUCK_ORPHAN	0x10	/* TS_BUSY set, no packet outstanding. */
#define	STUCK_ONCE	0x20	/* The same, after some event. */

static int
sleepers(void)
{
	int stuck = 0;

	if (ucs_state(PROC_REBOOT) == UCS_ASLEEP)
		stuck |= STUCK_REBOOT;
	if (ucs_state(PROC_WRITER) == UCS_ASLEEP)
		stuck |= STUCK_WRITER;
	if (ucs_state(PROC_CLOSER) == UCS_ASLEEP)
		stuck |= STUCK_CLOSER;
	return stuck;
}

/*
 * Give the device every chance a reading host gives it: open the port, read
 * every armed packet, frame, type a carriage return, read again. A state
 * that keeps a sleeper or swallows the echo through that has no wakeup
 * source short of a kernel printf.
 */
static int
scenario_verdict(void)
{
	struct tty *tp = &usbttys[0];
	unsigned before;
	int stuck = 0, round;

	if (orphan_seen)
		stuck |= STUCK_ONCE;
	if (!host_open)
		event_apply(EV_OPEN);
	for (round = 0; round < 4; round++) {
		drain_in();
		event_apply(EV_SOF);
	}
	drain_in();
	if ((tp->t_state & TS_BUSY) && !usbd.tx_busy && !usbd.tx_pending)
		stuck |= STUCK_ORPHAN;
	/* A sleeper must wake from the drain alone, before any keystroke. */
	stuck |= sleepers();
	if ((tp->t_state & TS_ISOPEN) && (tp->t_flags & ECHO)) {
		before = host_bytes;
		event_apply(EV_CR);
		drain_in();
		event_apply(EV_SOF);
		drain_in();
		if (host_bytes == before)
			stuck |= STUCK_ECHO;
	}
	return stuck | sleepers();
}

static int
run_sequence(const enum event *seq, int len)
{
	int i;

	scenario_start();
	for (i = 0; i < len; i++) {
		if (!event_enabled(seq[i]))
			return -1;
		event_apply(seq[i]);
	}
	return scenario_verdict();
}

static char sequence_text[MAX_SEQUENCE * 16];

/* Name a sequence; a star marks an event outside the observed host's log. */
static const char *
sequence_name(const enum event *seq, int len)
{
	unsigned pos = 0;
	const char *name;
	int i;

	for (i = 0; i < len; i++) {
		if (i)
			sequence_text[pos++] = ' ';
		for (name = event_names[seq[i]]; *name; name++)
			sequence_text[pos++] = *name;
		if (!event_host_ok[seq[i]])
			sequence_text[pos++] = '*';
	}
	sequence_text[pos] = '\0';
	return sequence_text;
}

/* ------------------------------------------------------------------ */
/* Fixed sequences: each must leave no stuck state.                    */

struct fixed {
	const char	*name;
	enum event	seq[8];
	int		len;
};

static const struct fixed fixed_sequences[] = {
	{ "echo, then SET_CONFIGURATION with the echo packet in flight",
	    { EV_TYPE, EV_SETCONF }, 2 },
	{ "echo, then bus reset with the echo packet in flight",
	    { EV_TYPE, EV_RESET }, 2 },
	{ "echo, then SET_INTERFACE with the echo packet in flight",
	    { EV_TYPE, EV_SETIF }, 2 },
	{ "echo, then its bulk IN returns with no completion",
	    { EV_TYPE, EV_INLOST, EV_SOF }, 3 },
	{ "reboot(8) writes after a lost completion",
	    { EV_TYPE, EV_INLOST, EV_SOF, EV_REBOOT }, 4 },
	{ "writer past the high-water mark after SET_CONFIGURATION",
	    { EV_TYPE, EV_SETCONF, EV_WRITER }, 3 },
	{ "last close after SET_CONFIGURATION",
	    { EV_TYPE, EV_SETCONF, EV_CLOSER }, 3 },
	{ "port closed while a writer runs, port reopened",
	    { EV_CLOSE, EV_WRITER, EV_OPEN }, 3 },
	{ "writer above the high-water mark across a bus reset",
	    { EV_TYPE, EV_WRITER, EV_RESET }, 3 },
	{ "last close waiting across a bus reset",
	    { EV_WRITER, EV_CLOSER, EV_RESET }, 3 },
	{ "writer after a lost completion",
	    { EV_TYPE, EV_INLOST, EV_WRITER }, 3 },
	{ "last close after a lost completion",
	    { EV_TYPE, EV_INLOST, EV_CLOSER }, 3 },
	{ "reboot(8) behind a writer after a lost completion",
	    { EV_TYPE, EV_INLOST, EV_WRITER, EV_REBOOT }, 4 },
};

static int
run_fixed(void)
{
	unsigned i;
	int stuck;

	for (i = 0; i < sizeof(fixed_sequences) / sizeof(fixed_sequences[0]);
	    i++) {
		const struct fixed *f = &fixed_sequences[i];

		stuck = run_sequence(f->seq, f->len);
		hk_checks++;
		if (stuck != 0) {
			hk_note("FAIL %s: stuck 0x%x after %s", f->name, stuck,
			    sequence_name(f->seq, f->len));
			hk_failures++;
		} else
			hk_note("PASS %s", f->name);
	}
	return hk_verdict("usb console transmit state");
}

/* ------------------------------------------------------------------ */
/* Search.                                                             */

#define	NCLASSES	64

/* A random sample can be longer than an exhaustive one. */
static enum event found_seq[NCLASSES][MAX_SEQUENCE];
static int found_len[NCLASSES];
static unsigned long found_count[NCLASSES];
static unsigned long runs;
static char excluded[EV_COUNT];

static void
record(int stuck, const enum event *seq, int len)
{
	int i;

	if (stuck <= 0)
		return;
	found_count[stuck]++;
	if (found_len[stuck] != 0 && found_len[stuck] <= len)
		return;
	found_len[stuck] = len;
	for (i = 0; i < len; i++)
		found_seq[stuck][i] = seq[i];
}

static void
search(enum event *seq, int len, int depth)
{
	int e, stuck;

	runs++;
	stuck = run_sequence(seq, len);
	if (stuck < 0)
		return;
	record(stuck, seq, len);
	if (len == depth)
		return;
	for (e = 0; e < EV_COUNT; e++) {
		if (excluded[e])
			continue;
		seq[len] = (enum event)e;
		search(seq, len + 1, depth);
	}
}

static void
report_classes(void)
{
	int c;

	hk_note("%lu sequences run", runs);
	for (c = 1; c < NCLASSES; c++) {
		if (found_count[c] == 0)
			continue;
		hk_note("class 0x%02x%s%s%s%s%s%s: %lu sequences, shortest: %s",
		    c, (c & STUCK_ONCE) ? " orphaned" : "",
		    (c & STUCK_ORPHAN) ? " orphan" : "",
		    (c & STUCK_ECHO) ? " echo-latched" : "",
		    (c & STUCK_REBOOT) ? " reboot-asleep" : "",
		    (c & STUCK_WRITER) ? " writer-asleep" : "",
		    (c & STUCK_CLOSER) ? " close-asleep" : "",
		    found_count[c], sequence_name(found_seq[c], found_len[c]));
	}
}

static unsigned long long rng_state;

static unsigned
rng_next(void)
{
	rng_state = rng_state * 6364136223846793005ULL +
	    1442695040888963407ULL;
	return (unsigned)(rng_state >> 33);
}

static void
random_search(unsigned long seed, unsigned long count, int length)
{
	enum event seq[MAX_SEQUENCE];
	unsigned long n;
	int i, len, stuck;

	rng_state = seed;
	for (n = 0; n < count; n++) {
		scenario_start();
		len = 0;
		for (i = 0; i < length; i++) {
			enum event e = (enum event)(rng_next() % EV_COUNT);

			if (excluded[e] || !event_enabled(e))
				continue;
			seq[len++] = e;
			event_apply(e);
		}
		runs++;
		stuck = scenario_verdict();
		record(stuck, seq, len);
	}
}

static long
parse_number(const char *s)
{
	long v = 0;

	if (*s == '\0')
		return -1;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return -1;
		v = v * 10 + (*s - '0');
	}
	return v;
}

int
main(int argc, char **argv)
{
	enum event seq[MAX_DEPTH];
	long depth, seed, count, length;

	int a, e;

	if (argc == 1)
		return run_fixed();
	/* Trailing "-NAME" arguments drop an event from the alphabet. */
	while (argc > 2 && argv[argc - 1][0] == '-' && argv[argc - 1][1] != '-') {
		for (e = 0; e < EV_COUNT; e++)
			if (hk_streq(argv[argc - 1] + 1, event_names[e]))
				break;
		if (e == EV_COUNT)
			return 2;
		excluded[e] = 1;
		argc--;
	}
	for (a = 0, e = 0; e < EV_COUNT; e++)
		if (excluded[e])
			hk_note("excluded: %s", event_names[e]);
	(void)a;
	if (argc == 3 && hk_streq(argv[1], "--search")) {
		depth = parse_number(argv[2]);
		if (depth < 0 || depth > MAX_DEPTH)
			return 2;
		search(seq, 0, (int)depth);
		hk_note("search depth %ld over %d events", depth, EV_COUNT);
		report_classes();
		return 0;
	}
	if (argc == 5 && hk_streq(argv[1], "--random")) {
		seed = parse_number(argv[2]);
		count = parse_number(argv[3]);
		length = parse_number(argv[4]);
		if (seed < 0 || count < 0 || length < 1 || length > MAX_SEQUENCE)
			return 2;
		random_search((unsigned long)seed, (unsigned long)count,
		    (int)length);
		hk_note("random seed %ld, %ld sequences of %ld events",
		    seed, count, length);
		report_classes();
		return 0;
	}
	hk_note("usage: usb_console_test [--search DEPTH | "
	    "--random SEED COUNT LENGTH]");
	return 2;
}
