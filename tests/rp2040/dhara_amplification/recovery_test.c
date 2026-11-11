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
#include <setjmp.h>

#ifndef RECOVERY_TORN
#define RECOVERY_TORN 0
#endif

/* Reuse the calibrated NAND geometry, patterns and software traffic model. */
#define main amplification_main
#define dhara_nand_prog model_prog
#define dhara_nand_erase model_erase
#define dhara_nand_copy model_copy
#include "dhara_amplification_test.c"
#undef main
#undef dhara_nand_prog
#undef dhara_nand_erase
#undef dhara_nand_copy

static jmp_buf power_cut;
static unsigned point, target, acknowledged, started, erases_seen;
static int armed;

static void
boundary(void)
{
    if (armed && ++point == target)
        longjmp(power_cut, 1);
}

int
dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
    const uint8_t *data, dhara_error_t *err)
{
    int result;

    boundary();
#ifdef RECOVERY_SKIP_META
    if (armed && data[0] == 'D' && data[1] == 'h' && data[2] == 'a')
        return 0;
#endif
    if (armed && point + 1 == target && RECOVERY_TORN) {
        memcpy(region + (size_t)p * FLASH_UNIT_BYTES, data,
            FLASH_UNIT_BYTES / 2);
    }
    boundary();
    result = model_prog(n, p, data, err);
#ifdef RECOVERY_CORRUPT
    if (armed && result == 0)
        region[(size_t)p * FLASH_UNIT_BYTES] ^= 1;
#endif
    boundary();
    return result;
}

int
dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b,
    dhara_error_t *err)
{
    int result;

    boundary();
    if (armed) erases_seen++;
    if (armed && point + 1 == target && RECOVERY_TORN) {
        memset(region + (size_t)b * FLASH_ERASE_BYTES, 0xff,
            FLASH_ERASE_BYTES / 2);
    }
    boundary();
    result = model_erase(n, b, err);
    boundary();
    return result;
}

int
dhara_nand_copy(const struct dhara_nand *n, dhara_page_t src,
    dhara_page_t dst, dhara_error_t *err)
{
    uint8_t data[FLASH_UNIT_BYTES];

    if (dhara_nand_read(n, src, 0, sizeof data, data, err) < 0)
        return -1;
    return dhara_nand_prog(n, dst, data, err);
}

static int
transaction(struct dhara_map *m)
{
    unsigned i;

    for (i = 0; i < 16; i++) {
        started |= 1U << i;
        if (write_sector(m, i, 1) < 0)
            return -1;
#ifndef RECOVERY_ACK_EARLY
        if (sync_map(m) < 0)
            return -1;
#endif
        boundary();
        acknowledged |= 1U << i;
        boundary();
    }
    return 0;
}

static int
recover_and_check(struct dhara_map *m, uint8_t *page, unsigned count)
{
    unsigned i;
    uint8_t old[FLASH_UNIT_BYTES], next[FLASH_UNIT_BYTES], got[FLASH_UNIT_BYTES];
    dhara_error_t err = DHARA_E_NONE;

    memset(m, 0, sizeof *m);
    memset(page, 0, FLASH_UNIT_BYTES);
    dhara_map_init(m, &nand, page, FLASH_GC_RATIO);
    if (dhara_map_resume(m, &err) < 0) {
        fprintf(stderr, "recovery: map resume error %d\n", (int)err);
        return 1;
    }
    for (i = 0; i < count; i++) {
        int old_ok, new_ok;

        pattern(old, i, 0);
        pattern(next, i, 1);
        if (dhara_map_read(m, i, got, &err) < 0) {
            fprintf(stderr, "recovery: sector %u read error %d\n", i, (int)err);
            return 1;
        }
        old_ok = memcmp(old, got, sizeof got) == 0;
        new_ok = memcmp(next, got, sizeof got) == 0;
        if (i < 16 && (acknowledged & (1U << i))) {
            if (!new_ok) {
                fprintf(stderr, "recovery: acknowledged sector %u differs\n", i);
                return 1;
            }
        } else if (i < 16 && (started & (1U << i))) {
            if (!old_ok && !new_ok) {
                fprintf(stderr, "recovery: mixed sector %u\n", i);
                return 1;
            }
        } else if (!old_ok) {
            fprintf(stderr, "recovery: unchanged sentinel %u differs\n", i);
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    static struct dhara_map map;
    static uint8_t page[FLASH_UNIT_BYTES];
    uint8_t *snapshot;
    unsigned count, i, total;

    region = malloc(FLASH_FS_BYTES);
    snapshot = malloc(FLASH_FS_BYTES);
    if (region == NULL || snapshot == NULL) return 2;
    fresh(&map, page);
    count = (unsigned)dhara_map_capacity(&map);
    for (i = 0; i < count; i++)
        if (write_sector(&map, i, 0) < 0) return 2;
    if (sync_map(&map) < 0) return 2;
    memcpy(snapshot, region, FLASH_FS_BYTES);
    point = acknowledged = started = erases_seen = 0;
    target = (unsigned)-1;
    armed = 1;
    if (transaction(&map) < 0) return 2;
    armed = 0;
    total = point;
    if (recover_and_check(&map, page, count) || erases_seen == 0) {
        fprintf(stderr, "FAIL recovery completed transaction\n");
        return 1;
    }
    for (target = 1; target <= total; target++) {
        memcpy(region, snapshot, FLASH_FS_BYTES);
        acknowledged = started = point = 0;
        if (recover_and_check(&map, page, count)) return 1;
        armed = 1;
        if (setjmp(power_cut) == 0) {
            if (transaction(&map) < 0) return 2;
            return 2; /* Every enumerated boundary must cut. */
        }
        armed = 0;
        if (recover_and_check(&map, page, count)) {
            fprintf(stderr, "FAIL recovery cut %u/%u ack=%x started=%x\n",
                target, total, acknowledged, started);
            return 1;
        }
    }
    printf("Dhara recovery: %u interrupted boundaries, %u logical sectors\n",
        total, count);
    free(snapshot);
    free(region);
    return 0;
}
