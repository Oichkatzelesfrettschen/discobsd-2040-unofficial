/*
 * RK11 controller with one RK05 drive on a file. The pack is 203
 * cylinders of 2 surfaces of 12 sectors of 512 bytes, and the file is the
 * pack in that order; a read past the end of the file yields zeros, so a
 * short or sparse file is a pack whose unwritten sectors are empty.
 *
 * Registers, from the RK11-D manual:
 *   RKDS 0777400  drive status        RKER 0777402  error
 *   RKCS 0777404  control and status  RKWC 0777406  word count
 *   RKBA 0777410  bus address         RKDA 0777412  disk address
 *
 * An error sets its RKER bit and the RKCS error bits, ends the transfer,
 * and interrupts if enabled; that is what the V6 driver retries on and
 * reports as "err on dev rk0".
 */
#include <fcntl.h>
#include <unistd.h>

#include "pdp11.h"

#define RKCS_GO		(1 << 0)
#define RKCS_FN		(7 << 1)
#define RKCS_MEX	(3 << 4)
#define RKCS_IDE	(1 << 6)
#define RKCS_RDY	(1 << 7)
#define RKCS_HE		(1 << 14)
#define RKCS_ERR	(1 << 15)
#define RKCS_WMASK	017517		/* bits a program may set */

#define RKDS_SC		017
#define RKDS_RWSRDY	(1 << 6)
#define RKDS_DRY	(1 << 7)
#define RKDS_RK05	(1 << 11)

#define RKER_WCE	(1 << 0)
#define RKER_NXS	(1 << 5)
#define RKER_NXC	(1 << 6)
#define RKER_NXD	(1 << 7)
#define RKER_NXM	(1 << 10)
#define RKER_OVR	(1 << 14)
#define RKER_DRE	(1 << 15)

#define FN_RESET	0
#define FN_WRITE	1
#define FN_READ		2
#define FN_WCHK		3
#define FN_SEEK		4
#define FN_RCHK		5
#define FN_DRESET	6
#define FN_WLOCK	7

#define NCYL	203
#define NSURF	2
#define NSECT	12
#define SECBYTES 512

static uint16_t RKDS, RKER, RKCS, RKWC, RKBA_LO;
static uint8_t RKBA_HI;
static uint16_t drive, cylinder, surface, sector;
static int rkfd = -1;
static uint8_t secbuf[SECBYTES];

int
rk_open(const char *path)
{
	rkfd = open(path, O_RDWR);
	if (rkfd < 0)
		rkfd = open(path, O_RDONLY);
	return rkfd;
}

void
rk_close(void)
{
	if (rkfd >= 0)
		close(rkfd);
	rkfd = -1;
}

void
rk_reset(void)
{
	RKDS = RKDS_RK05 | RKDS_DRY | RKDS_RWSRDY;
	RKER = 0;
	RKCS = RKCS_RDY;
	RKWC = 0;
	RKBA_LO = 0;
	RKBA_HI = 0;
	drive = cylinder = surface = sector = 0;
}

static void
done(void)
{
	RKDS |= RKDS_RWSRDY;
	RKCS |= RKCS_RDY;
	if (RKCS & RKCS_IDE)
		cpu_interrupt(INTRK, 5);
}

static void
error(uint16_t e)
{
	RKER |= e;
	RKCS |= RKCS_ERR | RKCS_HE;
	done();
}

static uint32_t
busaddr(void)
{
	return ((uint32_t)RKBA_HI << 16) | RKBA_LO;
}

static void
setbusaddr(uint32_t a)
{
	RKBA_LO = a & 0xFFFF;
	RKBA_HI = (a >> 16) & 3;
}

static void
advance(void)
{
	if (++sector >= NSECT) {
		sector = 0;
		if (++surface >= NSURF) {
			surface = 0;
			cylinder++;
		}
	}
}

/* One sector of transfer; returns 0 on success, or the RKER bit. */
static uint16_t
xfer_sector(int w)
{
	uint32_t pos = ((uint32_t)cylinder * NSURF * NSECT +
	    (uint32_t)surface * NSECT + sector) * SECBYTES;
	uint32_t ba = busaddr();
	int n, i;
	uint16_t v;

	if (cylinder >= NCYL)
		return RKER_OVR;
	if (lseek(rkfd, (off_t)pos, SEEK_SET) < 0)
		return RKER_DRE;
	if (w) {
		for (i = 0; i < SECBYTES && RKWC != 0; i += 2) {
			if (dma_read16(ba, &v) < 0)
				return RKER_NXM;
			secbuf[i] = v & 0xFF;
			secbuf[i + 1] = v >> 8;
			ba += 2;
			RKWC++;
		}
		for (; i < SECBYTES; i++)
			secbuf[i] = 0;
		n = write(rkfd, secbuf, SECBYTES);
		if (n != SECBYTES)
			return RKER_DRE;
	} else {
		n = read(rkfd, secbuf, SECBYTES);
		if (n < 0)
			return RKER_DRE;
		for (; n < SECBYTES; n++)
			secbuf[n] = 0;
		for (i = 0; i < SECBYTES && RKWC != 0; i += 2) {
			v = secbuf[i] | (secbuf[i + 1] << 8);
			if (dma_write16(ba, v) < 0)
				return RKER_NXM;
			ba += 2;
			RKWC++;
		}
	}
	setbusaddr(ba);
	advance();
	return 0;
}

static void
go(void)
{
	int fn = (RKCS & RKCS_FN) >> 1;
	uint16_t e;

	RKCS &= ~(RKCS_RDY | RKCS_ERR | RKCS_HE);
	RKDS &= ~RKDS_RWSRDY;
	RKER = 0;
	switch (fn) {
	case FN_RESET:
		rk_reset();
		done();
		return;
	case FN_WRITE:
	case FN_READ:
		break;
	case FN_SEEK:
	case FN_DRESET:
	case FN_WLOCK:
		done();
		return;
	default:		/* write check and read check: unused by V6 */
		done();
		return;
	}
	if (rkfd < 0) {
		error(RKER_DRE);
		return;
	}
	if (drive != 0) {
		error(RKER_NXD);
		return;
	}
	if (cylinder >= NCYL) {
		error(RKER_NXC);
		return;
	}
	if (sector >= NSECT) {
		error(RKER_NXS);
		return;
	}
	while (RKWC != 0) {
		e = xfer_sector(fn == FN_WRITE);
		if (e) {
			error(e);
			return;
		}
	}
	done();
}

uint16_t
rk_read16(uint32_t a)
{
	switch (a) {
	case 0777400:
		return (RKDS & ~RKDS_SC) | (sector & RKDS_SC);
	case 0777402:
		return RKER;
	case 0777404:
		return RKCS | ((RKBA_HI & 3) << 4);
	case 0777406:
		return RKWC;
	case 0777410:
		return RKBA_LO;
	case 0777412:
		return sector | (surface << 4) | (cylinder << 5) | (drive << 13);
	}
	TRAP(INTBUS);
	return 0;
}

void
rk_write16(uint32_t a, uint16_t v)
{
	switch (a) {
	case 0777400:
	case 0777402:
		return;
	case 0777404:
		RKBA_HI = (v & RKCS_MEX) >> 4;
		RKCS = (RKCS & ~RKCS_WMASK) | (v & RKCS_WMASK & ~RKCS_GO);
		if (v & RKCS_GO)
			go();
		return;
	case 0777406:
		RKWC = v;
		return;
	case 0777410:
		RKBA_LO = v;
		return;
	case 0777412:
		drive = v >> 13;
		cylinder = (v >> 5) & 0377;
		surface = (v >> 4) & 1;
		sector = v & 017;
		return;
	}
	TRAP(INTBUS);
}
