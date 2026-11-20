/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * count: a QEMU TCG plugin that counts executed guest instructions by the
 * symbol that holds them, and prints the total and the largest counts on
 * exit. COUNT_FINE=symbol counts that symbol by instruction address
 * instead, and COUNT_TOP=n prints n lines (README.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <qemu-plugin.h>
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
#define NSYM 2048
static struct { char name[64]; struct qemu_plugin_scoreboard *sb; uint64_t v; } syms[NSYM];
static int nsym;
static qemu_plugin_u64 counter_for(const char *name) {
	int i;
	for (i = 0; i < nsym; i++) if (!strcmp(syms[i].name, name)) return qemu_plugin_scoreboard_u64(syms[i].sb);
	if (nsym == NSYM) return qemu_plugin_scoreboard_u64(syms[0].sb);
	snprintf(syms[nsym].name, sizeof syms[nsym].name, "%s", name);
	syms[nsym].sb = qemu_plugin_scoreboard_new(sizeof(uint64_t));
	return qemu_plugin_scoreboard_u64(syms[nsym++].sb);
}
static void tb_trans(struct qemu_plugin_tb *tb, void *u) {
	size_t n = qemu_plugin_tb_n_insns(tb), i;
	(void)u;
	for (i = 0; i < n; i++) {
		struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
		const char *s = qemu_plugin_insn_symbol(insn);
		char fine[64]; const char *want = getenv("COUNT_FINE");
		if (s && want && !strcmp(s, want)) { snprintf(fine, sizeof fine, "@%" PRIx64, (uint64_t)qemu_plugin_insn_vaddr(insn)); s = fine; }
		qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_ADD_U64, counter_for(s ? s : "?"), 1);
	}
}
static int cmp(const void *a, const void *b) { uint64_t x = *(const uint64_t *)((const char *)a + 72), y = *(const uint64_t *)((const char *)b + 72); return x < y ? 1 : x > y ? -1 : 0; }
static void at_exit(void *p) {
	uint64_t total = 0; int i;
	(void)p;
	for (i = 0; i < nsym; i++) { syms[i].v = qemu_plugin_u64_sum(qemu_plugin_scoreboard_u64(syms[i].sb)); total += syms[i].v; }
	qsort(syms, nsym, sizeof syms[0], cmp);
	fprintf(stderr, "TOTAL %" PRIu64 "\n", total);
	for (i = 0; i < nsym && i < (getenv("COUNT_TOP") ? atoi(getenv("COUNT_TOP")) : 26); i++) fprintf(stderr, "%6.2f%% %12" PRIu64 " %s\n", 100.0 * syms[i].v / total, syms[i].v, syms[i].name);
}
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv) {
	(void)info; (void)argc; (void)argv;
	qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans, NULL);
	qemu_plugin_register_atexit_cb(id, at_exit, NULL);
	return 0;
}
