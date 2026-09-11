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
 * carries 2 MB and 264 KB, so the kernel regions below are larger than the
 * reference target and the user region is the same size.
 */

MEMORY {
    BOOT2   (rx)  : ORIGIN = 0x10000000, LENGTH = 256    /* Second stage.   */
    FLASH   (rx)  : ORIGIN = 0x10000100, LENGTH = 512K - 256

    /*
     * The root filesystem occupies flash above the kernel, reached through
     * dev/flash.c rather than executed, so it is declared here to keep the
     * kernel from growing into it. 512K matches the STM32F407XE kernel
     * region, leaving 1536K for the filesystem.
     */
    FSFLASH (r)   : ORIGIN = 0x10080000, LENGTH = 1536K

    /*
     * User space sits at the bottom of SRAM because lib/elf32-arm.ld links
     * every userland executable at 0x20000000 and machparam.h's
     * USER_DATA_START names the same address on every Arm target. 96K is
     * the STM32 figure, kept so a process image swaps in the same size.
     */
    USERRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 96K    /* User space.     */

    RAM     (rw!x): ORIGIN = 0x20018000, LENGTH = 154K   /* Kernel data.    */

    /*
     * Each u area is exactly USIZE, 3072 bytes, and the kernel stack of
     * the current process lives in its top; kern.ldscript puts _estack at
     * the end of UAREA. longjmp exchanges USIZE bytes between the two
     * when it switches processes, so a region larger than USIZE would
     * leave the stack above the exchanged part, shared by every process
     * and overwritten by whichever ran last.
     */
    U0AREA  (rw!x): ORIGIN = 0x2003E800, LENGTH = 3K     /* Process 0 u.    */
    UAREA   (rw!x): ORIGIN = 0x2003F400, LENGTH = 3K     /* Current u.      */

    /*
     * 0x20040000 - 0x20041FFF is SRAM4 and SRAM5, two unstriped 4 KB banks.
     * The boot ROM stages boot2 in SRAM5, so neither is claimed here.
     */
}
