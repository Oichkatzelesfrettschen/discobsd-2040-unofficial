/* ARMv6-M application tier: all image, heap and stack bytes fit 192 KiB. */
ENTRY(_start)
MEMORY { user (rwx) : ORIGIN = 0x20000000, LENGTH = 192K }
SECTIONS {
    .text : { *(.text*) *(.rodata*) } > user
    .ARM.exidx : { *(.ARM.exidx*) } > user
    .data : { *(.data*) } > user
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > user
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > user
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > user
    .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(8); } > user
    /DISCARD/ : { *(.comment) *(.note*) }
}
