ENTRY(workload)
SECTIONS {
  .text 0x10000100 : { *(.text.workload) *(.text*) }
  .rodata : { *(.rodata*) }
  .ARM.exidx : { *(.ARM.exidx*) }
  /DISCARD/ : { *(.comment) }
}
