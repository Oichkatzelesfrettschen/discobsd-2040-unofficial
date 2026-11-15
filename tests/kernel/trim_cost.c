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
 * Trim cost measurement over the trim_order_test harness (included with its
 * main() renamed): Dhara pages, erases and live count per workload for one
 * arm. The arm is chosen by the objects linked (old or new itrunc order,
 * trim in free() or not) and, for a deferred sweep, by a third argument K:
 * at each sync, trim the free-list entries deeper than K from the LIFO top,
 * never a chain node. docs/research/trim-cost-model.md reports the results.
 * Usage: trim_cost IMAGE ARM [KKK].
 */
#define main trim_order_main
#include "trim_order_test.c"
#undef main

static int sweep_k = -1;	/* >= 0: arm D, sweep deeper than K at sync */
static unsigned sweep_trims;

/* Arm D: trim free-list entries deeper than K from the LIFO top, skipping chain nodes. */
static void
sweep(int k)
{
	daddr_t list[NICFREE];
	int nfree, i, depth = 0;
	struct buf *bp;

	nfree = fs->fs_nfree;
	bcopy(fs->fs_free, list, sizeof(list));
	for (;;) {
		if (nfree <= 0 || nfree > NICFREE)
			return;
		for (i = nfree - 1; i >= 1; i--) {
			if (depth++ >= k && list[i] != 0) {
				sweep_trims++;
				bdev_trim(DEV, list[i]);
			}
		}
		if (list[0] == 0)
			return;
		bp = bread(DEV, list[0]);
		nfree = *(int *)bp->b_addr;
		bcopy((caddr_t)bp->b_addr + sizeof(int), list, sizeof(list));
		brelse(bp);
	}
}

static void
cost_sync(void)
{
	if (sweep_k >= 0)
		sweep(sweep_k);
	sync_root();
}

struct mark {
	unsigned pages, erases, wp, tp, sp, wc, tc, sc, meta, data, trims;
};

static void
mark(struct mark *m)
{
	m->pages = td_pages();
	m->erases = td_erases();
	m->wp = td_phase_pages(TD_WRITE);
	m->tp = td_phase_pages(TD_TRIM);
	m->sp = td_phase_pages(TD_SYNC);
	m->wc = td_phase_copies(TD_WRITE);
	m->tc = td_phase_copies(TD_TRIM);
	m->sc = td_phase_copies(TD_SYNC);
	m->meta = meta_writes;
	m->data = data_writes;
	m->trims = trims_issued;
}

static void
report(const char *arm, const char *work, const struct mark *a, unsigned blocks,
    unsigned live0, unsigned peak)
{
	struct mark b;

	mark(&b);
	hk_note("%s %s blocks=%u pages=%u per_block_x100=%u erases=%u "
	    "write_pages=%u write_copies=%u trim_pages=%u trim_copies=%u "
	    "sync_pages=%u sync_copies=%u meta_writes=%u data_writes=%u "
	    "trims=%u live0=%u peak=%u live_end=%u\n", arm, work, blocks,
	    b.pages - a->pages, (b.pages - a->pages) * 100 / blocks,
	    b.erases - a->erases, b.wp - a->wp, b.wc - a->wc, b.tp - a->tp,
	    b.tc - a->tc, b.sp - a->sp, b.sc - a->sc, b.meta - a->meta,
	    b.data - a->data, b.trims - a->trims, live0, peak, td_live());
}

static struct inode *
shipped_state(int static_blocks)
{
	struct inode *keep;

	fresh(0);
	keep = make_file(static_blocks);
	cost_sync();
	return keep;
}

static void
delete_file(struct inode *ip)
{
	ip->i_nlink = 0;
	iput(ip);
}

/* Overwrite logical block lbn of ip in place. */
static void
rewrite(struct inode *ip, daddr_t lbn, int seed)
{
	struct buf *bp;
	daddr_t bn = bmap(ip, lbn, B_READ, 0);

	bp = getblk(DEV, bn);
	pattern((u_char *)bp->b_addr, ip->i_number + seed, lbn);
	bwrite(bp);
}

int
main(int argc, char **argv)
{
	struct inode *keep, *ip[8], *big, *out;
	struct mark m;
	unsigned live0, peak;
	int r, k, n;
	const char *arm;

	image = argv[1];
	arm = argv[2];
	sweep_k = -1;
	if (argc > 3)
		sweep_k = (argv[3][0] - '0') * 100 + (argv[3][1] - '0') * 10 +
		    (argv[3][2] - '0');

	/* W1a: one 16-block file per round, 150 rounds. */
	keep = shipped_state(745);
	live0 = td_live(); peak = live0; mark(&m);
	for (r = 0; r < 150; r++) {
		ip[0] = make_file(16);
		delete_file(ip[0]);
		cost_sync();
		if (td_live() > peak) peak = td_live();
	}
	report(arm, "w1a-rounds-1x16", &m, 150 * 16, live0, peak);
	iput(keep);

	/* W1b: six 30-block files per round, 20 rounds. */
	keep = shipped_state(745);
	live0 = td_live(); peak = live0; mark(&m);
	for (r = 0; r < 20; r++) {
		for (k = 0; k < 6; k++)
			ip[k] = make_file(30);
		cost_sync();
		if (td_live() > peak) peak = td_live();
		for (k = 0; k < 6; k++)
			delete_file(ip[k]);
		cost_sync();
	}
	report(arm, "w1b-rounds-6x30", &m, 20 * 6 * 30, live0, peak);
	iput(keep);

	/* W2: compile-like: 5 temporaries of 30 deleted before the round ends, 20 blocks of output rewritten; one sync per round. */
	keep = shipped_state(725);
	out = make_file(20);
	cost_sync();
	live0 = td_live(); peak = live0; mark(&m);
	for (r = 0; r < 20; r++) {
		for (k = 0; k < 5; k++)
			ip[k] = make_file(30);
		for (n = 0; n < 20; n++)
			rewrite(out, n, r);
		for (k = 0; k < 5; k++)
			delete_file(ip[k]);
		cost_sync();
		if (td_live() > peak) peak = td_live();
	}
	report(arm, "w2-compile", &m, 20 * 170, live0, peak);
	iput(out);
	iput(keep);

	/* W3: delete one 150-block file, then 500 rewrites of existing blocks, sync every 20. */
	keep = shipped_state(595);
	big = make_file(150);
	cost_sync();
	live0 = td_live(); peak = live0; mark(&m);
	delete_file(big);
	cost_sync();
	for (n = 0; n < 500; n++) {
		rewrite(keep, (n * 37) % 595, n);
		if (n % 20 == 19)
			cost_sync();
		if (td_live() > peak) peak = td_live();
	}
	cost_sync();
	report(arm, "w3-delete-then-rewrite", &m, 500, live0, peak);
	/* W3 continued: the next 3000 rewrites, steady state. */
	mark(&m);
	for (n = 500; n < 3500; n++) {
		rewrite(keep, (n * 37) % 595, n);
		if (n % 20 == 19)
			cost_sync();
	}
	cost_sync();
	report(arm, "w3-next-3000-rewrites", &m, 3000, live0, peak);
	iput(keep);

	/* W4: the small 11-live filesystem, ten rounds of a 16-block file. */
	fresh(0);
	cost_sync();
	live0 = td_live(); peak = live0; mark(&m);
	for (r = 0; r < 10; r++) {
		ip[0] = make_file(16);
		delete_file(ip[0]);
		cost_sync();
		if (td_live() > peak) peak = td_live();
	}
	report(arm, "w4-small", &m, 160, live0, peak);
	return hk_failures != 0;
}
