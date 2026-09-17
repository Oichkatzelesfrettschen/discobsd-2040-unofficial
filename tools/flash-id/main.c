// Reports the QSPI flash identity of an RP2040 board over USB CDC.
//
// The RP2040 boot ROM reports neither flash size nor board model, and picotool
// infers flash size from a resident binary's metadata, so a board carrying no
// image yields neither. The flash chip answers for itself: JEDEC opcode 0x9f
// returns manufacturer, memory type, and a capacity exponent, and Winbond
// opcode 0x4b returns the 64-bit unique identifier that the boot ROM publishes
// as the USB serial number. Both are read-only opcodes, and the binary links
// as no_flash so it executes from SRAM and leaves flash contents untouched.

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "pico/stdlib.h"

// Read Identification: opcode plus three returned bytes.
#define CMD_JEDEC_ID 0x9fu
#define JEDEC_TOTAL 4u

// Read Unique ID: opcode, four dummy bytes, eight identifier bytes.
#define CMD_RUID 0x4bu
#define RUID_DUMMY 4u
#define RUID_DATA 8u
#define RUID_TOTAL (1u + RUID_DUMMY + RUID_DATA)

// A capacity exponent outside this range denies a meaningful shift, and marks a
// failed read rather than a small or large part.
#define CAPACITY_LOG_MIN 16u
#define CAPACITY_LOG_MAX 31u

int main(void) {
    stdio_init_all();

    uint8_t jedec_tx[JEDEC_TOTAL] = {CMD_JEDEC_ID};
    uint8_t jedec_rx[JEDEC_TOTAL] = {0};
    flash_do_cmd(jedec_tx, jedec_rx, JEDEC_TOTAL);

    uint8_t ruid_tx[RUID_TOTAL] = {CMD_RUID};
    uint8_t ruid_rx[RUID_TOTAL] = {0};
    flash_do_cmd(ruid_tx, ruid_rx, RUID_TOTAL);

    const uint8_t manufacturer = jedec_rx[1];
    const uint8_t memory_type = jedec_rx[2];
    const uint8_t capacity_log = jedec_rx[3];
    const bool capacity_valid =
        capacity_log >= CAPACITY_LOG_MIN && capacity_log <= CAPACITY_LOG_MAX;
    const unsigned long capacity_bytes =
        capacity_valid ? (1ul << capacity_log) : 0ul;

    while (true) {
        printf("JEDEC_ID %02x %02x %02x\n", manufacturer, memory_type,
               capacity_log);
        if (capacity_valid) {
            printf("FLASH_BYTES %lu\n", capacity_bytes);
            printf("FLASH_MIB %lu\n", capacity_bytes >> 20);
        } else {
            printf("FLASH_BYTES unreadable\n");
            printf("FLASH_MIB unreadable\n");
        }
        printf("UNIQUE_ID ");
        for (unsigned i = 0; i < RUID_DATA; i++) {
            printf("%02x", ruid_rx[1u + RUID_DUMMY + i]);
        }
        printf("\n");
        printf("SYS_CLK_HZ %lu\n", (unsigned long)clock_get_hz(clk_sys));
        printf("END\n");
        sleep_ms(1000);
    }
}
