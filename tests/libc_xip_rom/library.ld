OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(__aeabi_dadd)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS
{
    . = 0x10020000;
    .text : { *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    __flash_end = ALIGN(., 4);
    .data 0 : AT(__flash_end) { *(.data .data.*) } :data
    .got : AT(__flash_end + ADDR(.got)) { *(.got) } :data
    __libc_got_size = SIZEOF(.got);
    .got.plt : AT(__flash_end + ADDR(.got.plt)) { *(.got.plt) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) . = ALIGN(8); } :data
    __state_end = .;
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab*) }
}
