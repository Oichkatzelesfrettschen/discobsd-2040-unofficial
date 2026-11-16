/* Replay typed logical-byte traffic through the existing Dhara NOR model. */
#define main amplification_entry
#define dhara_nand_read traffic_base_read
#define dhara_nand_copy traffic_base_copy
#include "../rp2040/dhara_amplification/dhara_amplification_test.c"
#undef main
#undef dhara_nand_read
#undef dhara_nand_copy

static struct dhara_map traffic_map;
static uint8_t traffic_metadata[FLASH_UNIT_BYTES];
static uint8_t traffic_page[FLASH_UNIT_BYTES];
static size_t traffic_length;
static unsigned long long traffic_reads, traffic_read_bytes, traffic_copies;
static unsigned long long traffic_checkpoints;

int
dhara_nand_read(const struct dhara_nand *device, dhara_page_t page, size_t offset,
    size_t length, uint8_t *data, dhara_error_t *error)
{
    int result = traffic_base_read(device, page, offset, length, data, error);

    if (result == 0) {
        traffic_reads++;
        traffic_read_bytes += length;
    }
    return result;
}

int
dhara_nand_copy(const struct dhara_nand *device, dhara_page_t source,
    dhara_page_t destination, dhara_error_t *error)
{
    uint8_t data[FLASH_UNIT_BYTES];

    if (dhara_nand_read(device, source, 0, sizeof(data), data, error) < 0)
        return -1;
    traffic_copies++;
    return dhara_nand_prog(device, destination, data, error);
}

int
storage_dhara_setup(const uint8_t *initial, size_t length)
{
    dhara_error_t error = DHARA_E_NONE;
    dhara_sector_t sector, count;
    size_t offset, copied;

    if (region == NULL)
        region = malloc(FLASH_FS_BYTES);
    if (region == NULL)
        return -1;
    fresh(&traffic_map, traffic_metadata);
    count = dhara_map_capacity(&traffic_map);
    if (length > (size_t)count * FLASH_UNIT_BYTES)
        return -1;
    traffic_length = length;
    for (sector = 0; sector < count; sector++) {
        pattern(traffic_page, sector, 0);
        offset = (size_t)sector * FLASH_UNIT_BYTES;
        copied = offset < length ? length - offset : 0;
        if (copied > FLASH_UNIT_BYTES)
            copied = FLASH_UNIT_BYTES;
        if (copied)
            memcpy(traffic_page, initial + offset, copied);
        if (dhara_map_write(&traffic_map, sector, traffic_page, &error) < 0)
            return -1;
    }
    if (dhara_map_sync(&traffic_map, &error) < 0)
        return -1;
    page_programs = block_erases = 0;
    traffic_reads = traffic_read_bytes = traffic_copies = traffic_checkpoints = 0;
    return 0;
}

int
storage_dhara_transfer(size_t offset, uint8_t *data, size_t length,
    int writing, int synchronize)
{
    dhara_error_t error = DHARA_E_NONE;
    dhara_sector_t sector;
    size_t displacement, amount;

    if (offset > traffic_length || length > traffic_length - offset)
        return -1;
    while (length) {
        sector = (dhara_sector_t)(offset / FLASH_UNIT_BYTES);
        displacement = offset % FLASH_UNIT_BYTES;
        amount = FLASH_UNIT_BYTES - displacement;
        if (amount > length)
            amount = length;
        if (dhara_map_read(&traffic_map, sector, traffic_page, &error) < 0)
            return -1;
        if (writing) {
            memcpy(traffic_page + displacement, data, amount);
            if (dhara_map_write(&traffic_map, sector, traffic_page, &error) < 0)
                return -1;
            if (synchronize) {
                if (dhara_map_sync(&traffic_map, &error) < 0)
                    return -1;
                traffic_checkpoints++;
            }
        } else {
            memcpy(data, traffic_page + displacement, amount);
        }
        offset += amount;
        data += amount;
        length -= amount;
    }
    return 0;
}

int
storage_dhara_resume(void)
{
    dhara_error_t error = DHARA_E_NONE;

    memset(&traffic_map, 0, sizeof(traffic_map));
    memset(traffic_metadata, 0, sizeof(traffic_metadata));
    dhara_map_init(&traffic_map, &nand, traffic_metadata, FLASH_GC_RATIO);
    return dhara_map_resume(&traffic_map, &error);
}

void
storage_dhara_counts(unsigned long long *counts)
{
    counts[0] = page_programs * (FLASH_UNIT_BYTES / FLASH_PROG_BYTES);
    counts[1] = block_erases * (FLASH_ERASE_BYTES / FLASH_SECTOR_BYTES);
    counts[2] = traffic_checkpoints;
    counts[3] = traffic_reads;
    counts[4] = traffic_read_bytes;
    counts[5] = traffic_copies * FLASH_UNIT_BYTES;
}

int
storage_dhara_verify(const uint8_t *expected, size_t length)
{
    uint8_t wanted[FLASH_UNIT_BYTES], actual[FLASH_UNIT_BYTES];
    dhara_error_t error = DHARA_E_NONE;
    dhara_sector_t sector, count = dhara_map_capacity(&traffic_map);
    size_t offset, copied;

    if (length != traffic_length)
        return -1;
    for (sector = 0; sector < count; sector++) {
        pattern(wanted, sector, 0);
        offset = (size_t)sector * FLASH_UNIT_BYTES;
        copied = offset < length ? length - offset : 0;
        if (copied > FLASH_UNIT_BYTES)
            copied = FLASH_UNIT_BYTES;
        if (copied)
            memcpy(wanted, expected + offset, copied);
        if (dhara_map_read(&traffic_map, sector, actual, &error) < 0)
            return -1;
        if (memcmp(wanted, actual, sizeof(wanted)) != 0)
            return 1;
    }
    return 0;
}

void
storage_dhara_close(void)
{
    free(region);
    region = NULL;
}
