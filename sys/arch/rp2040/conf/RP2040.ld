/*
 * Memory map for the RP2040, as fitted on a Raspberry Pi Pico.
 *
 * Flash is external QSPI reached through the XIP window at 0x10000000. The
 * boot ROM reads a 256-byte second stage from offset 0, verifies a CRC32
 * trailer, and runs it from SRAM to configure XIP, so the kernel image starts
 * at 0x10000100 and the first 256 bytes belong to boot2.
 *
 * SRAM is 264 KB. The first 256 KB at 0x20000000 is four banks striped across
 * the bus for parallelism, transparent at that alias and used here. The two
 * 4 KB banks above it are left alone, because the boot ROM copies boot2 into
 * the topmost one.
 *
 * DiscoBSD asks for at least 128 KB of flash and 128 KB of RAM, reserving 32
 * KB of RAM for the kernel and 96 KB for userland. The STM32F407XE target
 * ships 512 KB of flash and 160 KB of RAM across its regions. This board
 * carries 2 MB and 264 KB, so every region below is larger than the reference
 * target rather than squeezed to fit.
 */

MEMORY {
    BOOT2   (rx)  : ORIGIN = 0x10000000, LENGTH = 256    /* Second stage.   */
    FLASH   (rx)  : ORIGIN = 0x10000100, LENGTH = 2048K - 256

    RAM     (rw!x): ORIGIN = 0x20000000, LENGTH = 120K   /* Kernel data.    */
    U0AREA  (rw!x): ORIGIN = 0x2001E000, LENGTH = 4K     /* Process 0 u.    */
    UAREA   (rw!x): ORIGIN = 0x2001F000, LENGTH = 4K     /* Current u.      */

    USERRAM (rwx) : ORIGIN = 0x20020000, LENGTH = 128K   /* User space.     */

    /*
     * 0x20040000 - 0x20041FFF is SRAM4 and SRAM5, two unstriped 4 KB banks.
     * The boot ROM stages boot2 in SRAM5, so neither is claimed here.
     */
}
