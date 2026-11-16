OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(shared_errno_address)
absolute_zero = 0;
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x10020000;
    .text : { *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    __libc_flash_end = ALIGN(., 4);
    .data 0 : AT(__libc_flash_end) { *(.data .data.*) } :data
    .got : AT(__libc_flash_end + ADDR(.got)) { *(.got) } :data
    .got.plt : AT(__libc_flash_end + ADDR(.got.plt)) { *(.got.plt) } :data
    __libc_init_end = .;
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) . = ALIGN(8); } :data
    __libc_state_size = .;
    _end = .;
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab*) }
}
