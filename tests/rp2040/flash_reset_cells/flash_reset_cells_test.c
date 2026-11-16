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
 * Host oracle for the processor-reset half of the recovery matrix (W03 of
 * docs/research/rp2040-memory-wear-engineering-program.md).
 *
 * It includes the NOR model of tests/rp2040/flash_checkpoint, which runs the
 * production flash.c driver text and the vendored Dhara map over a 2 MiB
 * part, and adds a cut: a processor reset stops the driver between two chip
 * commands or part way through the data clocked into a page program, and the
 * chip keeps its bytes.
 *
 * What a reset does to the chip (W25Q16JV datasheet, RP2040 datasheet 2.8):
 *   - A program or erase command that /CS has already closed is self-timed
 *     inside the part and completes: only the dedicated /RESET pin (absent
 *     from the Pico's package) and the 66h/99h reset command stop it, and
 *     the boot ROM issues neither. The boot ROM reads the second stage while
 *     the part is busy, the part ignores those reads, and the ROM retries for
 *     about 0.5 s, longer than the 400 ms sector erase maximum. A chip
 *     command is therefore atomic: either not started or complete.
 *   - A page program whose /CS rises on a byte boundary programs the bytes
 *     clocked so far and nothing else; /CS rising inside a byte discards the
 *     command. The cut model keeps a byte prefix of the 256-byte chip page.
 *   - The boot ROM's flash_range_program takes one chip command per 256-byte
 *     page and flash_range_erase one per 4 KiB sector, so a 1 KiB Dhara page
 *     program is four commands and an 8 KiB Dhara block erase is two. A reset
 *     can fall between them. Bits partly cleared or partly set belong to
 *     power loss, which this oracle does not model.
 *
 * Verdicts are separate. MAP: Dhara resumes the chip, and the resumed map
 * accepts writes, checkpoints and a further reset without a NOR fault.
 * CONTENT: the recovered logical blocks equal the state after some prefix of
 * the write history, and that prefix lies in the set cells.tsv permits for
 * the acknowledgment history. Filesystem checks above the block device
 * (fsck, UFS consistency) are not run here.
 */

#include <setjmp.h>
#include <rp2040/dev/flash_swap.h>

static void reset_hook_erase(unsigned offset, unsigned len);
static void reset_hook_program(unsigned offset, const unsigned char *data,
    unsigned len);

#define	MODEL_ERASE_HOOK(offset, len)		reset_hook_erase(offset, len)
#define	MODEL_PROGRAM_HOOK(offset, data, len)	\
	reset_hook_program(offset, data, len)
#define	main	flash_checkpoint_main
#include "flash_checkpoint_test.c"
#undef	main

/* ------------------------------------------------------------------ */
/* Chip commands, cuts and the recorded history                        */

#define	EV_ERASE	'E'	/* One 4 KiB sector erase. */
#define	EV_DATA		'D'	/* One chip page of a data page program. */
#define	EV_CKPT		'K'	/* One chip page of a checkpoint page program. */
#define	EV_COPY		'C'	/* One chip page of a Dhara copy. */
#define	MAX_EVENTS	4096
#define	MAX_WRITES	4096
#define	DHARA_PAGES_PER_BLOCK	8U
#define	OPEN_GROUP_LOSS	6U	/* STORAGE.md, "Checkpoint cost". */

struct event {
	char		kind;
	unsigned	cmd;		/* Chip command within its call. */
	unsigned	ncmd;
	unsigned	offset;
};

struct write {
	daddr_t		blk;
	unsigned	seed;
};

/* Everything the hook and the script runner keep; snapshots copy it whole. */
struct run {
	struct write	w[MAX_WRITES];
	unsigned	started;	/* Logical writes begun. */
	unsigned	completed;	/* Logical writes whose request returned. */
	unsigned	acked;		/* Writes covered by a returned ack. */
	unsigned	base_acked;	/* Writes the fixture's base made durable. */
	unsigned	d_done;		/* Data page programs completed. */
	unsigned	last_cover;	/* Writes under the last finished checkpoint. */
	unsigned	pending;	/* A checkpoint program finished, unpromoted. */
	unsigned	pending_cover;
	unsigned	fsck_next;
	unsigned	cold_next;
	unsigned long	copy_commands;
	unsigned long	ckpt_commands;
};

static struct run run;

/* A request or ack failed; the caller decides whether that is a verdict. */
static int step_error;

struct snapshot {
	struct run	run;
	u_char		chip[FLASH_TOTAL_BYTES];
	struct dhara_map map;
	u_char		page[FLASH_UNIT_BYTES];
	u_char		scratch[FLASH_PROG_BYTES];
	struct fldisk	drives[NFL];
	daddr_t		blocks;
	int		ready;
	int		owner;
	unsigned long	program_pages, erase_sectors, model_faults;
};

static struct snapshot snap;	/* The state when the operation under test begins. */
static struct snapshot probe_snap;	/* A scratch copy for the script's own use. */

static struct event evlog[MAX_EVENTS];
static unsigned nev;
static int recording;
static long cut_event = -1;
static unsigned cut_prefix;
static unsigned cut_pred_lo, cut_pred_hi;
static struct event cut_what;
static jmp_buf cut_env;
static const char *cell_name = "setup";

static void
snap_save_to(struct snapshot *s)
{
	s->run = run;
	memcpy(s->chip, nor, sizeof(nor));
	s->map = flmap;
	memcpy(s->page, flpage, sizeof(flpage));
	memcpy(s->scratch, flscratch, sizeof(flscratch));
	memcpy(s->drives, fldrives, sizeof(fldrives));
	s->blocks = flblocks;
	s->ready = flmap_ready;
	s->owner = flscratch_owner;
	s->program_pages = program_pages;
	s->erase_sectors = erase_sectors;
	s->model_faults = model_faults;
}

static void
snap_restore_from(const struct snapshot *s)
{
	run = s->run;
	memcpy(nor, s->chip, sizeof(nor));
	flmap = s->map;
	memcpy(flpage, s->page, sizeof(flpage));
	memcpy(flscratch, s->scratch, sizeof(flscratch));
	memcpy(fldrives, s->drives, sizeof(fldrives));
	flblocks = s->blocks;
	flmap_ready = s->ready;
	flscratch_owner = s->owner;
	program_pages = s->program_pages;
	erase_sectors = s->erase_sectors;
	model_faults = s->model_faults;
	spl_level = 0;
	nev = 0;
	recording = 0;
	cut_event = -1;
}

static void
snap_save(void)
{
	snap_save_to(&snap);
}

static void
snap_restore(void)
{
	snap_restore_from(&snap);
}

/* A checkpoint program that finished before this point now covers its writes. */
static void
note_boundary(void)
{
	if (run.pending) {
		run.last_cover = run.pending_cover;
		run.pending = 0;
	}
}

static char
classify_program(unsigned offset, unsigned len)
{
	unsigned page;

	if (len == FLASH_PROG_BYTES)
		return EV_COPY;
	page = (offset - (unsigned)FLASH_FS_OFFSET) / (unsigned)FLASH_UNIT_BYTES;
	return page % DHARA_PAGES_PER_BLOCK == DHARA_PAGES_PER_BLOCK - 1 ?
	    EV_CKPT : EV_DATA;
}

/*
 * One chip command is about to start. Returns nonzero when it is the armed
 * cut, after recording what the cut leaves for the checkpoint-cover bound.
 */
static int
command_start(char kind, unsigned cmd, unsigned ncmd, unsigned offset)
{
	struct event ev;

	note_boundary();
	ev.kind = kind;
	ev.cmd = cmd;
	ev.ncmd = ncmd;
	ev.offset = offset;
	if (recording && nev < MAX_EVENTS)
		evlog[nev] = ev;
	if (recording)
		nev++;
	if (kind == EV_COPY)
		run.copy_commands++;
	else if (kind == EV_CKPT)
		run.ckpt_commands++;
	if (cut_event >= 0 && recording && (long)(nev - 1) == cut_event) {
		cut_what = ev;
		cut_pred_lo = run.last_cover;
		cut_pred_hi = run.last_cover;
		return 1;
	}
	return 0;
}

static void
reset_hook_erase(unsigned offset, unsigned len)
{
	unsigned c, ncmd = len / (unsigned)FLASH_SECTOR_BYTES;

	for (c = 0; c < ncmd; c++) {
		unsigned at = offset + c * (unsigned)FLASH_SECTOR_BYTES;

		if (command_start(EV_ERASE, c, ncmd, at)) {
			unsigned k;

			/* A finished command completed inside the part. */
			for (k = 0; k < c; k++)
				memset(nor + offset +
				    k * FLASH_SECTOR_BYTES, 0xff,
				    FLASH_SECTOR_BYTES);
			spl_level = 0;
			longjmp(cut_env, 1);
		}
	}
}

static void
reset_hook_program(unsigned offset, const unsigned char *data, unsigned len)
{
	unsigned c, ncmd = len / (unsigned)FLASH_PROG_BYTES;
	char kind = classify_program(offset, len);

	for (c = 0; c < ncmd; c++) {
		unsigned at = offset + c * (unsigned)FLASH_PROG_BYTES;

		if (command_start(kind, c, ncmd, at)) {
			unsigned k, i;

			for (k = 0; k < c; k++)
				for (i = 0; i < FLASH_PROG_BYTES; i++)
					nor[offset + k * FLASH_PROG_BYTES +
					    i] &= data[k * FLASH_PROG_BYTES +
					    i];
			for (i = 0; i < cut_prefix; i++)
				nor[at + i] &= data[c * FLASH_PROG_BYTES + i];
			if (kind == EV_CKPT && (c > 0 || cut_prefix > 0))
				cut_pred_hi = run.d_done;
			spl_level = 0;
			longjmp(cut_env, 1);
		}
	}
	note_boundary();
	if (kind == EV_DATA)
		run.d_done++;
	else if (kind == EV_CKPT) {
		run.pending = 1;
		run.pending_cover = run.d_done;
	}
}

/* ------------------------------------------------------------------ */
/* Scripts                                                             */

#define	UNIVERSE_LOW	24
#define	FSCK_BLOCKS	3
#define	FSCK_FIRST	64
#define	FSCK_SEED	1000U
#define	COLD_BLOCKS	6
#define	COLD_FIRST	400

static unsigned
workload_block(unsigned index)
{
	return (index * 7U) % UNIVERSE_LOW;
}

/*
 * The block payload of a write. fill() repeats with the seed modulo 256 and
 * the workload revisits a block every 24 writes, so two writes 768 apart
 * would carry equal data; the first eight bytes carry the whole seed and the
 * block, which makes every write's payload distinct.
 */
static void
payload(u_char *data, daddr_t blk, unsigned seed)
{
	fill(data, blk, seed);
	data[0] = (u_char)seed;
	data[1] = (u_char)(seed >> 8);
	data[2] = (u_char)(seed >> 16);
	data[3] = (u_char)(seed >> 24);
	data[4] = (u_char)blk;
	data[5] = (u_char)((unsigned long)blk >> 8);
	data[6] = (u_char)((unsigned long)blk >> 16);
	data[7] = (u_char)((unsigned long)blk >> 24);
}

/* kind: 0 workload block, 1 fsck repair block, 2 cold block never rewritten. */
static void
step_write(int kind)
{
	struct write *w;
	u_char data[DEV_BSIZE];
	int status;

	if (run.started >= MAX_WRITES) {
		step_error = 1;
		return;
	}
	w = &run.w[run.started];
	if (kind == 1) {
		w->blk = (daddr_t)(FSCK_FIRST + run.fsck_next % FSCK_BLOCKS);
		w->seed = FSCK_SEED + run.fsck_next % FSCK_BLOCKS;
		run.fsck_next++;
	} else if (kind == 2) {
		w->blk = (daddr_t)(COLD_FIRST + run.cold_next % COLD_BLOCKS);
		w->seed = 1 + run.started;
		run.cold_next++;
	} else {
		w->blk = (daddr_t)workload_block(run.started);
		w->seed = 1 + run.started;
	}
	run.started++;
	payload(data, w->blk, w->seed);
	status = transfer(ROOT_DEV, 0, w->blk, data);
	if (status != 0)
		step_error = 1;
	run.completed++;
	note_boundary();
}

static void
step_ack(void)
{
	if (checkpoint() != 0)
		step_error = 1;
	note_boundary();
	run.acked = run.completed;
}

static void
step_open(void)
{
	expect(flopen(ROOT_DEV, FREAD | FWRITE, 0) == 0, cell_name,
	    "open for writing");
}

static void
step_close(void)
{
	if (flclose(ROOT_DEV, FREAD | FWRITE, 0) != 0)
		step_error = 1;
	note_boundary();
	run.acked = run.completed;
}

/* Journal pages queued for collection: a write now would copy a page. */
static void
step_until_collection(void)
{
	unsigned long before;

	while (!step_error) {
		before = run.copy_commands;
		snap_save_to(&probe_snap);
		step_write(0);
		if (run.copy_commands != before) {
			snap_restore_from(&probe_snap);
			return;
		}
	}
}

/* As step_until_collection, for a write that also closes a checkpoint group. */
static void
step_until_collection_checkpoint(void)
{
	unsigned long copies, checkpoints;

	while (!step_error) {
		copies = run.copy_commands;
		checkpoints = run.ckpt_commands;
		snap_save_to(&probe_snap);
		step_write(0);
		if (run.copy_commands != copies &&
		    run.ckpt_commands != checkpoints) {
			snap_restore_from(&probe_snap);
			return;
		}
	}
}

/*
 * Write until the journal head has wrapped past the end of the region and
 * stands on a block boundary three blocks in: the next write then erases a
 * block that still holds the previous lap's pages, checkpoint page included.
 */
static void
step_until_wrap(void)
{
	unsigned prev = flmap.journal.head, cur;
	int wrapped = 0;

	while (!step_error) {
		step_write(0);
		cur = flmap.journal.head;
		if (cur < prev)
			wrapped = 1;
		prev = cur;
		if (wrapped && cur % DHARA_PAGES_PER_BLOCK == 0 &&
		    cur >= 3 * DHARA_PAGES_PER_BLOCK)
			return;
	}
}

static void step_swap(char kind);
static void remount_after_reset(void);

/*
 * Run the steps of a script or an operation: B base (three writes and an
 * ack), W write, F fsck write, Y cold write (a block never rewritten, which
 * leaves a live page at the journal tail for Dhara to copy), A ack
 * (DIOCCHECKPOINT), O open for writing, X close, Z write until the next write
 * collects, Q the same for a write that also closes a checkpoint group, R
 * reset without a cut, N write until the journal has wrapped, U fill the swap
 * sectors, S raw swap image append, T raw swap rewrite through the spare
 * sector. "W*6" repeats a step.
 */
static void
run_steps(const char *steps)
{
	const char *p = steps;

	while (*p != '\0') {
		char s = *p++;
		unsigned count = 1;

		if (s == ' ')
			continue;
		if (*p == '*') {
			count = (unsigned)strtoul(p + 1, (char **)&p, 10);
		}
		while (count-- > 0) {
			switch (s) {
			case 'B':
				step_write(0);
				step_write(0);
				step_write(0);
				step_ack();
				run.base_acked = run.acked;
				break;
			case 'W':
				step_write(0);
				break;
			case 'F':
				step_write(1);
				break;
			case 'Y':
				step_write(2);
				break;
			case 'A':
				step_ack();
				break;
			case 'O':
				step_open();
				break;
			case 'X':
				step_close();
				break;
			case 'Z':
				step_until_collection();
				break;
			case 'Q':
				step_until_collection_checkpoint();
				break;
			case 'N':
				step_until_wrap();
				break;
			case 'U':
			case 'S':
			case 'T':
				step_swap(s);
				break;
			case 'R':
				remount_after_reset();
				break;
			default:
				panic("unknown script step");
			}
		}
	}
}


/* ------------------------------------------------------------------ */
/* Raw swap: flash_swap.c over the swap half of the chip               */

#define	SWAP_APPEND_SECTOR	2U
#define	SWAP_APPEND_BYTES	5000U
#define	SWAP_REWRITE_SECTOR	5U
#define	SWAP_REWRITE_OFFSET	512U
#define	SWAP_REWRITE_BYTES	1024U
#define	SWAP_FILL_SECTORS	((unsigned)(FLASH_SWAP_BYTES / FLASH_SECTOR_BYTES))
#define	SWAP_SPARE		((unsigned)FLASH_SWAP_SCRATCH_OFFSET)

static unsigned char
swap_pattern(unsigned offset, unsigned salt)
{
	return (unsigned char)(offset * 29U + salt * 53U + (offset >> 8));
}

static int
swap_in_region(unsigned offset, unsigned len)
{
	return offset >= FLASH_SWAP_OFFSET && offset < FLASH_TOTAL_BYTES &&
	    len <= FLASH_TOTAL_BYTES - offset;
}

static int
swap_read(unsigned offset, unsigned char *data, unsigned len)
{
	if (!swap_in_region(offset, len))
		return -1;
	memcpy(data, nor + offset, len);
	return 0;
}

static int
swap_erase(unsigned offset, unsigned len)
{
	unsigned c, ncmd = len / (unsigned)FLASH_SECTOR_BYTES;

	if (offset % FLASH_SECTOR_BYTES != 0 || len % FLASH_SECTOR_BYTES != 0 ||
	    !swap_in_region(offset, len)) {
		model_fault("swap erase outside the swap region");
		return -1;
	}
	for (c = 0; c < ncmd; c++) {
		unsigned at = offset + c * (unsigned)FLASH_SECTOR_BYTES;

		if (command_start(EV_ERASE, c, ncmd, at)) {
			unsigned k;

			for (k = 0; k < c; k++)
				memset(nor + offset + k * FLASH_SECTOR_BYTES,
				    0xff, FLASH_SECTOR_BYTES);
			spl_level = 0;
			longjmp(cut_env, 1);
		}
	}
	memset(nor + offset, 0xff, len);
	return 0;
}

static int
swap_program(unsigned offset, const unsigned char *data, unsigned len)
{
	unsigned c, i, ncmd = len / (unsigned)FLASH_PROG_BYTES;

	if (offset % FLASH_PROG_BYTES != 0 || len % FLASH_PROG_BYTES != 0 ||
	    !swap_in_region(offset, len)) {
		model_fault("swap program outside the swap region");
		return -1;
	}
	for (i = 0; i < len; i++)
		if (nor[offset + i] != 0xff) {
			model_fault("swap program over unerased flash");
			break;
		}
	for (c = 0; c < ncmd; c++) {
		unsigned at = offset + c * (unsigned)FLASH_PROG_BYTES;

		if (command_start(EV_DATA, c, ncmd, at)) {
			unsigned k;

			for (k = 0; k < c; k++)
				for (i = 0; i < FLASH_PROG_BYTES; i++)
					nor[offset + k * FLASH_PROG_BYTES +
					    i] &= data[k * FLASH_PROG_BYTES +
					    i];
			for (i = 0; i < cut_prefix; i++)
				nor[at + i] &= data[c * FLASH_PROG_BYTES + i];
			spl_level = 0;
			longjmp(cut_env, 1);
		}
	}
	for (i = 0; i < len; i++)
		nor[offset + i] &= data[i];
	return 0;
}

static const struct flash_swap_ops swap_ops = {
	swap_read,
	swap_erase,
	swap_program,
};

static void
step_swap(char kind)
{
	static unsigned char image[SWAP_APPEND_BYTES];
	static unsigned char update[SWAP_REWRITE_BYTES];
	unsigned i, base;
	int status = 0;

	if (kind == 'U') {
		for (i = 0; i < SWAP_FILL_SECTORS * FLASH_SECTOR_BYTES; i++)
			nor[FLASH_SWAP_OFFSET + i] = swap_pattern(i, 1);
		return;
	}
	spl_level = 1;
	if (kind == 'S') {
		for (i = 0; i < sizeof(image); i++)
			image[i] = swap_pattern(i, 2);
		base = FLASH_SWAP_OFFSET + SWAP_APPEND_SECTOR *
		    (unsigned)FLASH_SECTOR_BYTES;
		status = flash_swap_append(&swap_ops, base, image,
		    sizeof(image), flscratch);
	} else {
		for (i = 0; i < sizeof(update); i++)
			update[i] = swap_pattern(i, 3);
		base = FLASH_SWAP_OFFSET + SWAP_REWRITE_SECTOR *
		    (unsigned)FLASH_SECTOR_BYTES + SWAP_REWRITE_OFFSET;
		status = flash_swap_rewrite(&swap_ops, base, update,
		    sizeof(update), SWAP_SPARE, flscratch);
	}
	spl_level = 0;
	if (status != 0)
		step_error = 1;
	note_boundary();
}

/*
 * What a swap operation may leave after a cut: no byte of the root region
 * moves, no swap sector outside the operation's own moves, and for a rewrite
 * every page the request leaves alone survives in the destination or the
 * spare sector. At completion the result equals the request.
 */
static int
swap_check(char kind, int completed, char *what, size_t size)
{
	const unsigned char *before = snap.chip;
	unsigned sector, i, page;
	unsigned dest = FLASH_SWAP_OFFSET + SWAP_REWRITE_SECTOR *
	    (unsigned)FLASH_SECTOR_BYTES;
	unsigned first = FLASH_SWAP_OFFSET + SWAP_APPEND_SECTOR *
	    (unsigned)FLASH_SECTOR_BYTES;
	unsigned last = first + 2 * (unsigned)FLASH_SECTOR_BYTES;

	if (memcmp(before, nor, FLASH_SWAP_OFFSET) != 0) {
		snprintf(what, size, "the root region changed");
		return 1;
	}
	for (sector = 0; sector < SWAP_FILL_SECTORS; sector++) {
		unsigned at = FLASH_SWAP_OFFSET + sector *
		    (unsigned)FLASH_SECTOR_BYTES;
		int allowed = kind == 'S' ? (at >= first && at < last) :
		    (at == dest || at == SWAP_SPARE);

		if (!allowed && memcmp(before + at, nor + at,
		    FLASH_SECTOR_BYTES) != 0) {
			snprintf(what, size, "swap sector %u changed outside "
			    "the operation's sectors", sector);
			return 1;
		}
	}
	if (kind == 'T') {
		for (page = 0; page < FLASH_SECTOR_BYTES;
		    page += FLASH_PROG_BYTES) {
			int touched = page + FLASH_PROG_BYTES >
			    SWAP_REWRITE_OFFSET &&
			    page < SWAP_REWRITE_OFFSET + SWAP_REWRITE_BYTES;

			if (touched)
				continue;
			if (memcmp(before + dest + page, nor + dest + page,
			    FLASH_PROG_BYTES) != 0 &&
			    memcmp(before + dest + page, nor + SWAP_SPARE +
			    page, FLASH_PROG_BYTES) != 0) {
				snprintf(what, size, "page %u the request "
				    "leaves alone survives in neither the "
				    "destination nor the spare sector",
				    page / (unsigned)FLASH_PROG_BYTES);
				return 1;
			}
		}
	}
	if (completed) {
		if (kind == 'S') {
			for (i = 0; i < SWAP_APPEND_BYTES; i++)
				if (nor[first + i] != swap_pattern(i, 2)) {
					snprintf(what, size, "the appended "
					    "image differs at byte %u", i);
					return 1;
				}
		} else {
			for (i = 0; i < FLASH_SECTOR_BYTES; i++) {
				unsigned char want = before[dest + i];

				if (i >= SWAP_REWRITE_OFFSET && i <
				    SWAP_REWRITE_OFFSET + SWAP_REWRITE_BYTES)
					want = swap_pattern(i -
					    SWAP_REWRITE_OFFSET, 3);
				if (nor[dest + i] != want) {
					snprintf(what, size, "the rewritten "
					    "sector differs at byte %u", i);
					return 1;
				}
			}
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Remount and verdicts                                                */

#define	UNIVERSE_BLOCKS	(UNIVERSE_LOW + FSCK_BLOCKS + COLD_BLOCKS + 3)

static daddr_t
universe_block(unsigned i)
{
	if (i < UNIVERSE_LOW)
		return (daddr_t)i;
	if (i < UNIVERSE_LOW + FSCK_BLOCKS)
		return (daddr_t)(FSCK_FIRST + i - UNIVERSE_LOW);
	if (i < UNIVERSE_LOW + FSCK_BLOCKS + COLD_BLOCKS)
		return (daddr_t)(COLD_FIRST + i - UNIVERSE_LOW - FSCK_BLOCKS);
	return (daddr_t)(300 + 11 * (i - UNIVERSE_LOW - FSCK_BLOCKS -
	    COLD_BLOCKS));
}

/* Seed of the pattern the block holds: 0 blank, -1 matches no write. */
static long
block_seed(daddr_t blk)
{
	u_char got[DEV_BSIZE], want[DEV_BSIZE];
	unsigned i;

	if (transfer(ROOT_DEV, B_READ, blk, got) != 0)
		return -2;
	memset(want, 0xff, sizeof(want));
	if (memcmp(got, want, sizeof(got)) == 0)
		return 0;
	for (i = run.started; i-- > 0;) {
		if (run.w[i].blk != blk)
			continue;
		payload(want, blk, run.w[i].seed);
		if (memcmp(got, want, sizeof(got)) == 0)
			return (long)run.w[i].seed;
	}
	return -1;
}

static long
state_seed(unsigned m, daddr_t blk)
{
	unsigned i;

	for (i = m; i-- > 0;)
		if (run.w[i].blk == blk)
			return (long)run.w[i].seed;
	return 0;
}

struct verdict {
	int		resume;		/* dhara_map_resume() result. */
	dhara_error_t	resume_error;
	long		m;		/* Prefix the blocks equal, or -1. */
	char		detail[160];
};

static struct dhara_map probe_map;
static u_char probe_page[FLASH_UNIT_BYTES];

/* Reset the driver's RAM and bring the chip up as a boot does. */
static void
remount(struct verdict *v)
{
	dhara_error_t err = DHARA_E_NONE;
	unsigned i, m;
	long seeds[UNIVERSE_BLOCKS];
	char *d = v->detail;

	spl_level = 0;
	run.pending = 0;
	dhara_map_init(&probe_map, &flnand, probe_page, FLASH_GC_RATIO);
	v->resume = dhara_map_resume(&probe_map, &err);
	v->resume_error = err;
	power_cycle(cell_name);
	v->m = -1;
	v->detail[0] = '\0';
	for (i = 0; i < UNIVERSE_BLOCKS; i++)
		seeds[i] = block_seed(universe_block(i));
	for (m = run.started + 1; m-- > 0;) {
		for (i = 0; i < UNIVERSE_BLOCKS; i++)
			if (seeds[i] != state_seed(m, universe_block(i)))
				break;
		if (i == UNIVERSE_BLOCKS) {
			v->m = (long)m;
			return;
		}
	}
	/* No prefix matches: say which blocks disagree with the best prefix. */
	for (i = 0; i < UNIVERSE_BLOCKS && d - v->detail < 120; i++)
		if (seeds[i] < 0 || seeds[i] != state_seed(run.completed,
		    universe_block(i)))
			d += snprintf(d, 40, " blk%ld=%ld",
			    (long)universe_block(i), seeds[i]);
}

static void
remount_after_reset(void)
{
	struct verdict v;
	unsigned long faults = model_faults;

	remount(&v);
	expect(v.resume == 0 && v.m >= 0, cell_name,
	    "a scripted reset leaves an unrecoverable root");
	/*
	 * Only the unacknowledged suffix may go: the history is rebased on
	 * the recovered prefix below, so a loss admitted here would hide
	 * from every later cut of the cell.
	 */
	expect(v.m < 0 || (unsigned)v.m >= run.acked, cell_name,
	    "a scripted reset loses an acknowledged write");
	if (v.m >= 0) {
		run.started = run.completed = run.acked = run.d_done =
		    run.last_cover = (unsigned)v.m;
		if (run.base_acked > run.acked)
			run.base_acked = run.acked;
	}
	expect(model_faults == faults, cell_name, "NOR contract kept");
}

/* ------------------------------------------------------------------ */
/* Cells                                                               */

struct cell {
	char	id[16];
	char	op[8];
	char	hist[8];
	char	script[48];
	char	opsteps[48];
	char	must[32];
	char	may[16];
	char	tear[16];
	char	status[24];
	char	basis[1024];
};

#define	MAX_CELLS	64
static struct cell cells[MAX_CELLS];
static unsigned ncells;

static const char *
field_copy(char *dst, size_t size, const char *src)
{
	size_t n = strcspn(src, "\t\n");

	if (n >= size)
		panic("cells.tsv field too long");
	memcpy(dst, src, n);
	dst[n] = '\0';
	return src[n] == '\t' ? src + n + 1 : src + n;
}

static void
load_cells(const char *path)
{
	char line[2048];
	FILE *f = fopen(path, "r");

	if (f == NULL)
		panic("cannot open cells.tsv");
	while (fgets(line, sizeof(line), f) != NULL) {
		struct cell *c;
		const char *p = line;

		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (ncells >= MAX_CELLS)
			panic("too many cells");
		c = &cells[ncells++];
		p = field_copy(c->id, sizeof(c->id), p);
		p = field_copy(c->op, sizeof(c->op), p);
		p = field_copy(c->hist, sizeof(c->hist), p);
		p = field_copy(c->script, sizeof(c->script), p);
		p = field_copy(c->opsteps, sizeof(c->opsteps), p);
		p = field_copy(c->must, sizeof(c->must), p);
		p = field_copy(c->may, sizeof(c->may), p);
		p = field_copy(c->tear, sizeof(c->tear), p);
		p = field_copy(c->status, sizeof(c->status), p);
		(void)field_copy(c->basis, sizeof(c->basis), p);
	}
	fclose(f);
}

/*
 * Known-good calibration: with FLASH_RESET_CELLS_ATOMIC_CKPT set, a cut
 * never falls inside a checkpoint page program, as on a part that programs
 * a 1 KiB page as one command. Every cell then passes.
 */
static int atomic_ckpt;

static const unsigned prefixes[] = { 0, 1, 3, 4, 7, 8, 16, 20, 64, 128, 255 };
#define	NPREFIX	(sizeof(prefixes) / sizeof(prefixes[0]))

struct tally {
	unsigned	cuts;
	unsigned	map_fail;
	unsigned	content_fail;
	unsigned	predict_fail;
	unsigned	tear_fail;	/* Failing cuts inside a checkpoint program. */
	unsigned	between_fail;	/* Of those, between two chip commands. */
	unsigned	other_fail;	/* Failing cuts anywhere else. */
	unsigned	first_fail_event;
	unsigned	first_fail_prefix;
	char		first_fail_what[320];
	char		first_between_what[320];
	unsigned	exact_states;	/* Distinct recovered prefixes seen. */
	long		seen[8];
};

static int
has(const char *list, const char *token)
{
	return strstr(list, token) != NULL;
}

/*
 * The repair runs again on the recovered root, as the next boot's fsck does:
 * it must reach its full result, the blocks it does not target keep their
 * contents, and the result survives one more reset. 1 when it does.
 */
static int
repair_converges(unsigned m)
{
	long before[UNIVERSE_BLOCKS];
	unsigned i;
	int good = 1;

	for (i = 0; i < UNIVERSE_BLOCKS; i++)
		before[i] = block_seed(universe_block(i));
	run.started = run.completed = run.acked = run.d_done = m;
	run.fsck_next = 0;
	step_error = 0;
	run_steps("F*3 A");
	good = step_error == 0;
	power_cycle(cell_name);
	for (i = 0; i < UNIVERSE_BLOCKS; i++) {
		daddr_t blk = universe_block(i);
		long seed = block_seed(blk);

		if (blk >= FSCK_FIRST && blk < FSCK_FIRST + FSCK_BLOCKS)
			good &= seed == (long)(FSCK_SEED +
			    (unsigned)(blk - FSCK_FIRST));
		else
			good &= seed == before[i];
	}
	return good;
}

/*
 * Judge one cut. Returns 0 when both the map and the content verdicts hold.
 * prediction is the exact set the checkpoint-cover model gives; the table's
 * must, may and tear columns give the documented bounds.
 */
static int
judge(const struct cell *c, struct tally *t, unsigned event, unsigned prefix,
    int tore, int completed)
{
	struct verdict v;
	unsigned lo, hi;
	unsigned long faults;
	char what[200];
	int bad = 0, map_bad = 0, content_bad = 0, predict_bad = 0;
	u_char data[DEV_BSIZE], got[DEV_BSIZE];
	unsigned n;

	if (!has(c->tear, "ckpt"))
		tore = 0;
	remount(&v);
	/* Documented bounds: the table's columns choose which terms apply. */
	lo = has(c->must, "base") ? run.base_acked : 0;
	if (has(c->must, "ack") && run.acked > lo)
		lo = run.acked;
	if (has(c->must, "group") && run.completed > OPEN_GROUP_LOSS &&
	    run.completed - OPEN_GROUP_LOSS > lo)
		lo = run.completed - OPEN_GROUP_LOSS;
	hi = has(c->may, "prefix") ? run.started : lo;
	if (hi < lo)
		hi = lo;
	what[0] = '\0';

	if (v.resume != 0) {
		map_bad = 1;
		snprintf(what, sizeof(what), "resume refused (error %d)",
		    (int)v.resume_error);
	}
	if (v.m < 0) {
		content_bad = 1;
		snprintf(what + strlen(what), sizeof(what) - strlen(what),
		    "%s no prefix of the history matches:%s",
		    what[0] ? ";" : "", v.detail);
	} else {
		if ((unsigned)v.m < lo || (unsigned)v.m > hi) {
			content_bad = 1;
			snprintf(what + strlen(what), sizeof(what) -
			    strlen(what), "%s recovered prefix %ld outside "
			    "[%u,%u]", what[0] ? ";" : "", v.m, lo, hi);
		}
		if ((unsigned)v.m != cut_pred_lo &&
		    !(tore && (unsigned)v.m == cut_pred_hi)) {
			predict_bad = 1;
			snprintf(what + strlen(what), sizeof(what) -
			    strlen(what), "%s prefix %ld, model predicted %u%s",
			    what[0] ? ";" : "", v.m, cut_pred_lo,
			    tore ? " or the checkpoint's" : "");
		}
	}

	if (c->op[0] == 'S' || c->op[0] == 'T') {
		char swap_what[120];

		if (swap_check(c->op[0], completed, swap_what,
		    sizeof(swap_what))) {
			content_bad = 1;
			snprintf(what + strlen(what), sizeof(what) -
			    strlen(what), "%s swap: %s", what[0] ? ";" : "",
			    swap_what);
		}
	}
	faults = model_faults;
	if (c->op[0] == 'F' && v.m >= 0 && !map_bad &&
	    !repair_converges((unsigned)v.m)) {
		content_bad = 1;
		snprintf(what + strlen(what), sizeof(what) - strlen(what),
		    "%s repeating the repair did not converge",
		    what[0] ? ";" : "");
	}

	/* The resumed map keeps working: writes, checkpoint, reset. */
	step_error = 0;
	if (v.m >= 0 && !map_bad) {
		run.started = run.completed = (unsigned)v.m;
		for (n = 0; n < 2; n++) {
			u_int blk = 200 + n;

			payload(data, (daddr_t)blk, 777 + n);
			if (transfer(ROOT_DEV, 0, (daddr_t)blk, data) != 0)
				map_bad = 1;
		}
		if (checkpoint() != 0)
			map_bad = 1;
		power_cycle(cell_name);
		for (n = 0; n < 2; n++) {
			u_int blk = 200 + n;

			payload(data, (daddr_t)blk, 777 + n);
			if (transfer(ROOT_DEV, B_READ, (daddr_t)blk, got) != 0 ||
			    memcmp(got, data, sizeof(got)) != 0)
				map_bad = 1;
		}
		if (map_bad)
			snprintf(what + strlen(what), sizeof(what) -
			    strlen(what), "%s resumed map failed to write and "
			    "recover", what[0] ? ";" : "");
	}
	if (model_faults != faults) {
		map_bad = 1;
		snprintf(what + strlen(what), sizeof(what) - strlen(what),
		    "%s NOR contract broken after resume", what[0] ? ";" : "");
	}

	t->cuts++;
	if (map_bad)
		t->map_fail++;
	if (content_bad)
		t->content_fail++;
	if (predict_bad)
		t->predict_fail++;
	bad = map_bad || content_bad || predict_bad;
	if (bad) {
		if (tore) {
			t->tear_fail++;
			if (prefix == 0) {
				if (t->between_fail++ == 0)
					snprintf(t->first_between_what,
					    sizeof(t->first_between_what),
					    "%c command %u/%u at flash+%#x "
					    "(%u earlier commands of the page "
					    "done): %s", cut_what.kind,
					    cut_what.cmd + 1, cut_what.ncmd,
					    cut_what.offset, cut_what.cmd,
					    what);
			}
		} else
			t->other_fail++;
	}
	if (bad && getenv("FLASH_RESET_CELLS_VERBOSE") != NULL)
		printf("  cut %c %u/%u prefix %u: map %d content %d predict %d: "
		    "%s\n", cut_what.kind, cut_what.cmd + 1, cut_what.ncmd,
		    prefix, map_bad, content_bad, predict_bad, what);
	if (bad && t->tear_fail + t->other_fail == 1) {
		t->first_fail_event = event;
		t->first_fail_prefix = prefix;
		snprintf(t->first_fail_what, sizeof(t->first_fail_what),
		    "%c command %u/%u at flash+%#x, prefix %u bytes: %s",
		    cut_what.kind, cut_what.cmd + 1, cut_what.ncmd,
		    cut_what.offset, prefix, what);
	}
	return bad;
}

static unsigned
record_events(const struct cell *c)
{
	snap_save();
	nev = 0;
	recording = 1;
	cut_event = -1;
	run_steps(c->opsteps);
	note_boundary();
	recording = 0;
	return nev;
}

/* Run the operation until the armed cut stops it; 1 when it did. */
static int
run_until_cut(const char *steps)
{
	volatile int cut = 1;

	recording = 1;
	nev = 0;
	if (setjmp(cut_env) == 0) {
		run_steps(steps);
		cut = 0;
	}
	recording = 0;
	cut_event = -1;
	return cut;
}

static void
run_cell(struct cell *c)
{
	struct tally t;
	unsigned events, e, j;
	int expect_defect = strcmp(c->status, "defect") == 0;

	memset(&t, 0, sizeof(t));
	cell_name = c->id;
	fresh_chip(c->id);
	memset(&run, 0, sizeof(run));
	step_error = 0;
	run_steps(c->script);
	note_boundary();
	if (step_error) {
		failures++;
		printf("FAIL %s: the script failed before the operation\n",
		    c->id);
		return;
	}
	snap_save();
	events = record_events(c);
	if (step_error) {
		failures++;
		printf("FAIL %s: the operation failed without a cut\n", c->id);
		return;
	}
	if (model_faults != 0) {
		failures++;
		printf("FAIL %s: the script and operation broke the NOR "
		    "contract %lu times\n", c->id, model_faults);
		return;
	}
	snap_restore();

	for (e = 0; e <= events; e++) {
		int is_program = e < events && evlog[e].kind != EV_ERASE;
		unsigned np = is_program ? (unsigned)NPREFIX : 1;

		for (j = 0; j < np; j++) {
			int tore = 0;

			snap_restore();
			if (atomic_ckpt && e < events &&
			    evlog[e].kind == EV_CKPT &&
			    (evlog[e].cmd > 0 || prefixes[j] > 0))
				continue;
			if (e < events) {
				cut_event = (long)e;
				cut_prefix = prefixes[j];
				if (!run_until_cut(c->opsteps))
					panic("armed cut did not fire");
				note_boundary();
				run.pending = 0;
				tore = evlog[e].kind == EV_CKPT &&
				    (evlog[e].cmd > 0 || prefixes[j] > 0);
			} else {
				run_steps(c->opsteps);
				note_boundary();
				cut_pred_lo = run.last_cover;
				cut_pred_hi = run.last_cover;
				cut_what.kind = '-';
				cut_what.cmd = 0;
				cut_what.ncmd = 0;
				cut_what.offset = 0;
			}
			step_error = 0;
			(void)judge(c, &t, e, prefixes[j], tore, e == events);
		}
	}
	printf("cell %s %s %s: %u chip commands, %u cuts, map failures %u, "
	    "content failures %u, prediction misses %u\n", c->id, c->op,
	    c->hist, events, t.cuts, t.map_fail, t.content_fail,
	    t.predict_fail);
	if (expect_defect && atomic_ckpt) {
		if (t.tear_fail + t.other_fail != 0) {
			failures++;
			printf("FAIL %s: %u failing cuts with atomic checkpoint "
			    "programs, first: %s\n", c->id,
			    t.tear_fail + t.other_fail, t.first_fail_what);
		}
	} else if (expect_defect) {
		if (t.other_fail != 0) {
			failures++;
			printf("FAIL %s: %u failing cuts outside a checkpoint "
			    "program, first: %s\n", c->id, t.other_fail,
			    t.first_fail_what);
		} else if (t.between_fail == 0) {
			failures++;
			printf("FAIL %s: the recorded defect no longer "
			    "reproduces between chip commands; update "
			    "cells.tsv\n", c->id);
		} else {
			printf("DEFECT %s: %u of %u cuts inside a checkpoint "
			    "program leave the permitted set, %u of them "
			    "between two chip commands; first of those: %s\n",
			    c->id, t.tear_fail, t.cuts, t.between_fail,
			    t.first_between_what);
		}
	} else if (t.tear_fail + t.other_fail != 0) {
		failures++;
		printf("FAIL %s: %u failing cuts, first: %s\n", c->id,
		    t.tear_fail + t.other_fail, t.first_fail_what);
	}
}

/*
 * Known-bad input for the content verdict: a state rolled back by 768 writes
 * carries fill() data equal to the current one, so only payload() tells them
 * apart.
 */
static void
calibrate_payload(void)
{
	u_char old_data[DEV_BSIZE], new_data[DEV_BSIZE];

	fill(old_data, 5, 1);
	fill(new_data, 5, 1 + 768);
	expect(memcmp(old_data, new_data, sizeof(old_data)) == 0, "payload",
	    "calibration premise: fill() repeats after 768 writes");
	payload(old_data, 5, 1);
	payload(new_data, 5, 1 + 768);
	expect(memcmp(old_data, new_data, sizeof(old_data)) != 0, "payload",
	    "payload() separates writes 768 apart");
}

int
main(int argc, char **argv)
{
	unsigned i;
	const char *only = argc > 2 ? argv[2] : NULL;

	if (argc < 2) {
		fprintf(stderr, "usage: flash_reset_cells cells.tsv [id]\n");
		return 2;
	}
	calibrate_payload();
	atomic_ckpt = getenv("FLASH_RESET_CELLS_ATOMIC_CKPT") != NULL;
	load_cells(argv[1]);
	for (i = 0; i < ncells; i++) {
		if (only != NULL && strcmp(only, cells[i].id) != 0)
			continue;
		if (strcmp(cells[i].status, "host") == 0 ||
		    strcmp(cells[i].status, "defect") == 0)
			run_cell(&cells[i]);
		else
			printf("cell %s %s %s: %s\n", cells[i].id,
			    cells[i].op, cells[i].hist, cells[i].status);
	}
	if (failures != 0) {
		printf("flash_reset_cells: %d expectations failed\n", failures);
		return 1;
	}
	printf("flash_reset_cells: all expectations met\n");
	return 0;
}
