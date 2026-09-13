/*
 * Shared Thumb-1 assembly forms for RP2040 Boot ROM floating-point calls.
 * The C preprocessor expands these forms before either GNU as or the
 * DiscoBSD native assembler sees them.
 */

#define ROM_FLOAT_SETUP \
	.syntax unified; \
	.cpu cortex-m0plus; \
	.thumb

#define ROM_FLOAT_TEXT \
	.text; \
	.p2align 2

#define ROM_FLOAT_FUNCTION(symbol) \
	.globl symbol; \
	.type symbol, %function; \
	.thumb_func; \
symbol:

#define ROM_FLOAT_FUNCTION_END(symbol) \
	.size symbol, . - symbol

#define ROM_FLOAT_ALIAS(alias, target) \
	.globl alias; \
	.type alias, %function; \
	.thumb_set alias, target

#define ROM_FLOAT_CELL_NAME(member) __rp2040_rom_float_##member##_cell
#define ROM_FLOAT_FIRST_NAME(member) Lrp2040_rom_float_##member##_first

/*
 * The resolver reads the descriptor at lr - 1, the byte after the bl, with
 * a word load, so the word must sit on a four-byte boundary. mov ip, lr is
 * two bytes and bl four; starting the trampoline two bytes past a word
 * boundary puts the descriptor at +8. The nop is that two-byte offset. A
 * descriptor at +6 faults the Cortex-M0+ on the unaligned ldr, which is
 * what a bare-cell member such as clz did when nothing preceded it.
 */
#define ROM_FLOAT_TARGET_DESCRIPTOR(member, table_code, table_offset, feature) \
	.p2align 2; \
	nop; \
	.type ROM_FLOAT_FIRST_NAME(member), %function; \
	.thumb_func; \
ROM_FLOAT_FIRST_NAME(member):; \
	mov ip, lr; \
	bl __rp2040_rom_float_resolve; \
	.word ROM_FLOAT_CELL_NAME(member); \
	.hword table_code; \
	.byte table_offset; \
	.byte feature; \
	.size ROM_FLOAT_FIRST_NAME(member), . - ROM_FLOAT_FIRST_NAME(member); \
	.pool; \
	.data; \
	.p2align 2; \
	.globl ROM_FLOAT_CELL_NAME(member); \
	.type ROM_FLOAT_CELL_NAME(member), %object; \
ROM_FLOAT_CELL_NAME(member):; \
	.word ROM_FLOAT_FIRST_NAME(member); \
	.size ROM_FLOAT_CELL_NAME(member), . - ROM_FLOAT_CELL_NAME(member)

#define ROM_FLOAT_TARGET_CELL(member, table_code, table_offset, feature) \
	ROM_FLOAT_TEXT; \
	ROM_FLOAT_TARGET_DESCRIPTOR(member, table_code, table_offset, feature)

#define ROM_FLOAT_SINGLE_TARGET(member, symbol, table_code, table_offset, feature) \
	ROM_FLOAT_TEXT; \
	ROM_FLOAT_FUNCTION(symbol); \
	ldr r2, =ROM_FLOAT_CELL_NAME(member); \
	ldr r3, [r2]; \
	bx r3; \
	ROM_FLOAT_FUNCTION_END(symbol); \
	ROM_FLOAT_TARGET_DESCRIPTOR(member, table_code, table_offset, feature)

#define ROM_FLOAT_DOUBLE_TARGET(member, symbol, table_code, table_offset, feature) \
	ROM_FLOAT_TEXT; \
	ROM_FLOAT_FUNCTION(symbol); \
	push {r3, r4}; \
	ldr r3, =ROM_FLOAT_CELL_NAME(member); \
	ldr r3, [r3]; \
	str r3, [sp, #4]; \
	pop {r3, pc}; \
	ROM_FLOAT_FUNCTION_END(symbol); \
	ROM_FLOAT_TARGET_DESCRIPTOR(member, table_code, table_offset, feature)
