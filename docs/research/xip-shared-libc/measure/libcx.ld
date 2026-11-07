
SECTIONS {
  . = 0x10100000;
  .text : { *(.text .text.*) *(.rodata .rodata.*) *(.glue_7 .glue_7t) }
  .ARM.exidx : { *(.ARM.exidx*) }
  __libc_data_load = .;
  . = 0x20000000;
  .data : AT(__libc_data_load) { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
  __libc_ram_end = .;
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
