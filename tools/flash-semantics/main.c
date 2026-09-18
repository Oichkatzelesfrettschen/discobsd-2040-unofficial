// Measures, on the board, the NOR flash behaviors an emulator model of the
// part has to reproduce, and reports each one over USB CDC as a line a host
// script can parse.
//
// Every claim a flash model makes is a claim about the part, and the part is
// on the board: the status register's WIP and WEL bits, how long a sector
// erase and a page program keep WIP set, what an erased sector reads, that a
// program can only clear bits, that a page program wraps inside its 256-byte
// page, and what the JEDEC and SFDP commands answer. The datasheet states
// each of these; this program reads them off the silicon so a report about
// the model can cite the measurement rather than the table.
//
// The image links no_flash and runs from SRAM. The one sector it erases and
// programs is the last 4 KB of the 128 KB kernel region, which the kernel
// image does not reach (BOOT-MAP.md section 2 puts the image at about 100 KB
// of the region), and the sector is left erased when the measurements end.
// The root filesystem at 0x10020000 and the swap above it are never
// addressed. After reporting, the watchdog reboots the board into whatever
// is resident in flash.

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/time.h"

// W25Q16JV instruction set: opcode, then what follows it on the wire.
#define CMD_WRITE_ENABLE 0x06u
#define CMD_WRITE_DISABLE 0x04u
#define CMD_READ_STATUS1 0x05u
#define CMD_READ_DATA 0x03u
#define CMD_PAGE_PROGRAM 0x02u
#define CMD_SECTOR_ERASE 0x20u
#define CMD_JEDEC_ID 0x9fu
#define CMD_READ_SFDP 0x5au

#define STATUS_WIP 0x01u
#define STATUS_WEL 0x02u

#define ADDRESS_BYTES 3u
#define PAGE_BYTES 256u
#define SECTOR_BYTES 4096u
#define SFDP_HEADER_BYTES 8u

// The last sector of the kernel region: FLASH_FS_OFFSET (flash.h) is 128 KB,
// and the kernel image ends about 27 KB below it.
#define SCRATCH_OFFSET 0x1f000u

// A poll loop that never sees WIP clear stops here and says so, so a part
// that stays busy cannot hang the report.
#define POLL_LIMIT_US 5000000ull

// Patterns whose AND is zero, so a second program over an unerased page
// shows whether the part clears bits or stores the raw byte.
#define PATTERN_FIRST 0x0fu
#define PATTERN_SECOND 0xf0u

// The wrap test programs 32 bytes starting 16 bytes before the page end.
#define WRAP_START 0xf0u
#define WRAP_BYTES 32u

static uint8_t read_status1(void) {
    uint8_t tx[2] = {CMD_READ_STATUS1, 0};
    uint8_t rx[2] = {0};
    flash_do_cmd(tx, rx, sizeof tx);
    return rx[1];
}

static void write_enable(void) {
    uint8_t tx = CMD_WRITE_ENABLE;
    uint8_t rx = 0;
    flash_do_cmd(&tx, &rx, 1);
}

static void write_disable(void) {
    uint8_t tx = CMD_WRITE_DISABLE;
    uint8_t rx = 0;
    flash_do_cmd(&tx, &rx, 1);
}

static void put_address(uint8_t *tx, uint32_t offset) {
    tx[1] = (uint8_t)(offset >> 16);
    tx[2] = (uint8_t)(offset >> 8);
    tx[3] = (uint8_t)offset;
}

// Reads count bytes at offset with the plain 0x03 read, which needs no dummy
// byte and no XIP cache flush.
static void read_data(uint32_t offset, uint8_t *out, size_t count) {
    static uint8_t tx[1 + ADDRESS_BYTES + SECTOR_BYTES];
    static uint8_t rx[1 + ADDRESS_BYTES + SECTOR_BYTES];
    memset(tx, 0, sizeof tx);
    tx[0] = CMD_READ_DATA;
    put_address(tx, offset);
    flash_do_cmd(tx, rx, 1 + ADDRESS_BYTES + count);
    memcpy(out, rx + 1 + ADDRESS_BYTES, count);
}

struct busy_measure {
    uint8_t first_poll;  // status on the first read after the command
    uint32_t polls;
    uint64_t microseconds;
    bool timed_out;
};

// Polls status register 1 until WIP clears, recording how the part answered
// on the first poll and how long the whole wait took.
static struct busy_measure wait_not_busy(void) {
    struct busy_measure m = {0};
    uint64_t start = time_us_64();
    m.first_poll = read_status1();
    m.polls = 1;
    while (m.first_poll & STATUS_WIP) {
        uint8_t status = read_status1();
        m.polls++;
        if (!(status & STATUS_WIP)) {
            break;
        }
        if (time_us_64() - start > POLL_LIMIT_US) {
            m.timed_out = true;
            break;
        }
    }
    m.microseconds = time_us_64() - start;
    return m;
}

static struct busy_measure sector_erase(uint32_t offset) {
    uint8_t tx[1 + ADDRESS_BYTES] = {CMD_SECTOR_ERASE};
    uint8_t rx[1 + ADDRESS_BYTES] = {0};
    put_address(tx, offset);
    write_enable();
    flash_do_cmd(tx, rx, sizeof tx);
    return wait_not_busy();
}

static struct busy_measure page_program(uint32_t offset, const uint8_t *data,
                                        size_t count) {
    static uint8_t tx[1 + ADDRESS_BYTES + PAGE_BYTES];
    static uint8_t rx[1 + ADDRESS_BYTES + PAGE_BYTES];
    tx[0] = CMD_PAGE_PROGRAM;
    put_address(tx, offset);
    memcpy(tx + 1 + ADDRESS_BYTES, data, count);
    write_enable();
    flash_do_cmd(tx, rx, 1 + ADDRESS_BYTES + count);
    return wait_not_busy();
}

static unsigned count_equal(const uint8_t *buf, size_t count, uint8_t value) {
    unsigned n = 0;
    for (size_t i = 0; i < count; i++) {
        n += buf[i] == value;
    }
    return n;
}

struct report {
    uint8_t jedec[3];
    uint8_t sfdp[SFDP_HEADER_BYTES];
    uint8_t status_idle, status_after_wren, status_after_wrdi;
    unsigned pre_erase_ff;
    struct busy_measure erase;
    uint8_t status_after_erase;
    unsigned erase_readback_ff;
    struct busy_measure program;
    uint8_t status_after_program;
    unsigned program_readback_first;
    struct busy_measure reprogram;
    unsigned reprogram_readback_and;
    unsigned wrap_tail_ok, wrap_head_ok, wrap_next_page_ff;
    struct busy_measure final_erase;
    unsigned final_erase_ff;
};

static void measure(struct report *r) {
    static uint8_t buf[SECTOR_BYTES];
    static uint8_t page[PAGE_BYTES];

    // Identity: 0x9F returns three bytes, 0x5A takes a 24-bit address and
    // one dummy byte before the parameter table, whose first four bytes are
    // the ASCII signature "SFDP".
    uint8_t jedec_tx[4] = {CMD_JEDEC_ID};
    uint8_t jedec_rx[4] = {0};
    flash_do_cmd(jedec_tx, jedec_rx, sizeof jedec_tx);
    memcpy(r->jedec, jedec_rx + 1, 3);

    uint8_t sfdp_tx[1 + ADDRESS_BYTES + 1 + SFDP_HEADER_BYTES] = {CMD_READ_SFDP};
    uint8_t sfdp_rx[sizeof sfdp_tx] = {0};
    flash_do_cmd(sfdp_tx, sfdp_rx, sizeof sfdp_tx);
    memcpy(r->sfdp, sfdp_rx + 1 + ADDRESS_BYTES + 1, SFDP_HEADER_BYTES);

    // WEL follows Write Enable and Write Disable on its own, with no erase
    // or program between them.
    r->status_idle = read_status1();
    write_enable();
    r->status_after_wren = read_status1();
    write_disable();
    r->status_after_wrdi = read_status1();

    // Erase: WIP and WEL during, both clear after, every byte 0xFF.
    read_data(SCRATCH_OFFSET, buf, SECTOR_BYTES);
    r->pre_erase_ff = count_equal(buf, SECTOR_BYTES, 0xff);
    r->erase = sector_erase(SCRATCH_OFFSET);
    r->status_after_erase = read_status1();
    read_data(SCRATCH_OFFSET, buf, SECTOR_BYTES);
    r->erase_readback_ff = count_equal(buf, SECTOR_BYTES, 0xff);

    // Program page 0 with one pattern, then again with its complement
    // without an erase between: a part that clears bits reads back zero.
    memset(page, PATTERN_FIRST, PAGE_BYTES);
    r->program = page_program(SCRATCH_OFFSET, page, PAGE_BYTES);
    r->status_after_program = read_status1();
    read_data(SCRATCH_OFFSET, buf, PAGE_BYTES);
    r->program_readback_first = count_equal(buf, PAGE_BYTES, PATTERN_FIRST);
    memset(page, PATTERN_SECOND, PAGE_BYTES);
    r->reprogram = page_program(SCRATCH_OFFSET, page, PAGE_BYTES);
    read_data(SCRATCH_OFFSET, buf, PAGE_BYTES);
    r->reprogram_readback_and =
        count_equal(buf, PAGE_BYTES, PATTERN_FIRST & PATTERN_SECOND);

    // Wrap: 32 bytes into page 1 starting 16 bytes before its end. The
    // part places bytes 16..31 at the start of page 1 and leaves page 2
    // erased; a model that advances linearly writes them into page 2.
    for (unsigned i = 0; i < WRAP_BYTES; i++) {
        page[i] = (uint8_t)i;
    }
    page_program(SCRATCH_OFFSET + PAGE_BYTES + WRAP_START, page, WRAP_BYTES);
    read_data(SCRATCH_OFFSET + PAGE_BYTES, buf, 2 * PAGE_BYTES);
    r->wrap_tail_ok = 0;
    r->wrap_head_ok = 0;
    for (unsigned i = 0; i < PAGE_BYTES - WRAP_START; i++) {
        r->wrap_tail_ok += buf[WRAP_START + i] == i;
    }
    for (unsigned i = PAGE_BYTES - WRAP_START; i < WRAP_BYTES; i++) {
        r->wrap_head_ok += buf[i - (PAGE_BYTES - WRAP_START)] == i;
    }
    r->wrap_next_page_ff =
        count_equal(buf + PAGE_BYTES, WRAP_BYTES - (PAGE_BYTES - WRAP_START), 0xff);

    // Leave the sector as it was found at best, erased at worst.
    r->final_erase = sector_erase(SCRATCH_OFFSET);
    read_data(SCRATCH_OFFSET, buf, SECTOR_BYTES);
    r->final_erase_ff = count_equal(buf, SECTOR_BYTES, 0xff);
}

static void print_busy(const char *name, const struct busy_measure *m) {
    printf("%s_FIRST_POLL %02x\n", name, m->first_poll);
    printf("%s_POLLS %lu\n", name, (unsigned long)m->polls);
    printf("%s_US %llu%s\n", name, (unsigned long long)m->microseconds,
           m->timed_out ? " timeout" : "");
}

static void print_report(const struct report *r) {
    printf("JEDEC_ID %02x %02x %02x\n", r->jedec[0], r->jedec[1], r->jedec[2]);
    printf("SFDP");
    for (unsigned i = 0; i < SFDP_HEADER_BYTES; i++) {
        printf(" %02x", r->sfdp[i]);
    }
    printf("\n");
    printf("STATUS1_IDLE %02x\n", r->status_idle);
    printf("STATUS1_AFTER_WREN %02x\n", r->status_after_wren);
    printf("STATUS1_AFTER_WRDI %02x\n", r->status_after_wrdi);
    printf("SCRATCH_OFFSET %06x\n", SCRATCH_OFFSET);
    printf("PRE_ERASE_FF %u of %u\n", r->pre_erase_ff, SECTOR_BYTES);
    print_busy("ERASE", &r->erase);
    printf("STATUS1_AFTER_ERASE %02x\n", r->status_after_erase);
    printf("ERASE_READBACK_FF %u of %u\n", r->erase_readback_ff, SECTOR_BYTES);
    print_busy("PROGRAM", &r->program);
    printf("STATUS1_AFTER_PROGRAM %02x\n", r->status_after_program);
    printf("PROGRAM_READBACK_%02X %u of %u\n", PATTERN_FIRST,
           r->program_readback_first, PAGE_BYTES);
    print_busy("REPROGRAM", &r->reprogram);
    printf("REPROGRAM_READBACK_%02X %u of %u\n", PATTERN_FIRST & PATTERN_SECOND,
           r->reprogram_readback_and, PAGE_BYTES);
    printf("WRAP_TAIL_OK %u of %u\n", r->wrap_tail_ok, PAGE_BYTES - WRAP_START);
    printf("WRAP_HEAD_OK %u of %u\n", r->wrap_head_ok,
           WRAP_BYTES - (PAGE_BYTES - WRAP_START));
    printf("WRAP_NEXT_PAGE_FF %u of %u\n", r->wrap_next_page_ff,
           WRAP_BYTES - (PAGE_BYTES - WRAP_START));
    print_busy("FINAL_ERASE", &r->final_erase);
    printf("FINAL_ERASE_FF %u of %u\n", r->final_erase_ff, SECTOR_BYTES);
    printf("END\n");
}

int main(void) {
    static struct report r;

    stdio_init_all();
    measure(&r);

    // USB CDC delivers nothing until the host opens the port, so the
    // report repeats for a while, then the watchdog returns the board to
    // the image resident in flash.
    for (unsigned i = 0; i < 40; i++) {
        print_report(&r);
        sleep_ms(500);
    }
    watchdog_reboot(0, 0, 100);
    while (true) {
        tight_loop_contents();
    }
}
