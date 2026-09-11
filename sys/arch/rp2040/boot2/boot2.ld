/*
 * The boot ROM copies the 256-byte second stage from flash offset 0 into
 * the top of SRAM5, above its own stack, and runs it there. The last four
 * bytes hold the CRC32, so 252 bytes of code fit. Datasheet section 2.8.1.3.
 */
MEMORY {
	SRAM (rx) : ORIGIN = 0x20041f00, LENGTH = 252
}

SECTIONS {
	. = ORIGIN(SRAM);
	.text : {
		_stage2_boot = .;
		*(.text)
	} > SRAM
}
