/**
  ******************************************************************************
  * @file      startup_stm32f407xx.s
  * @author    MCD Application Team
  * @version   V1.0.0
  * @date      17-February-2017
  * @brief     STM32F407xx Devices vector table for GCC based toolchains.
  *
  *            After Reset the Cortex-M4 processor is in Thread mode,
  *            priority is Privileged, and the Stack is set to Main.
  *
  *            This module performs:
  *                - Set initial SP,
  *                - Set initial PC == Reset_Handler,
  *                - Set vector table entries with exception ISR addresses,
  *                - Branch to main(), set up user mode, then run icode().
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2017 STMicroelectronics</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */


/*
 * Derived from sys/arch/stm32/stm32/locore0.S. Retargeted from Cortex-M4
 * (ARMv7E-M) to the RP2040's Cortex-M0+ (ARMv6-M). Changes from that file:
 *
 * - .cpu names cortex-m0plus. The original names cortex-m4, which overrides
 *   -mcpu on the command line and lets the assembler accept Thumb-2 encodings
 *   the target cannot execute.
 * - LDR into SP and LR becomes LDR into a low register followed by MOV,
 *   because ARMv6-M LDR (literal) admits only R0-R7 as the destination.
 * - Post-indexed STR becomes STR plus ADDS, because ARMv6-M has no writeback
 *   form of STR (immediate).
 * - ORRS with an immediate becomes MOVS into a scratch register followed by
 *   register ORRS, because ARMv6-M ORRS takes register operands only.
 * - The BASEPRI write is deleted. ARMv6-M defines no BASEPRI register, yet the
 *   assembler encodes MSR to its SYSm value 17 without complaint, so the
 *   original line assembles and then writes an unimplemented system register
 *   at run time. CPSIE I already clears the only mask ARMv6-M has, PRIMASK.
 * - The vector table drops the MemManage, BusFault, UsageFault, and DebugMon
 *   entries, which ARMv6-M does not define, and carries the RP2040's 32
 *   interrupts in place of the STM32F4 set.
 */

	.syntax	unified
	.cpu	cortex-m0plus
	.thumb

	.globl	g_pfnVectors
	.globl	Default_Handler

	/*
	 * Memory addresses defined in linker script.
	 */
	.word	__user_data_start	/* Start addr of user RAM. */
	.word	__user_data_end		/* End addr of user RAM. */
	.word	u_end			/* End addr of kernel RAM u. area. */
	.word	_sidata			/* Start addr for .data init values. */
	.word	_sdata			/* Start addr for .data section. */
	.word	_edata			/* End addr for .data section. */
	.word	_sbss			/* Start addr for .bss section. */
	.word	_ebss			/* End addr for .bss section. */

	.section	.text.Reset_Handler
	.weak	Reset_Handler
	.type	Reset_Handler, %function
Reset_Handler:
	ldr	r0, =_estack	/* ARMv6-M LDR (literal) targets R0-R7 only. */
	mov	sp, r0		/* Set Main Stack Pointer (MSP). */

	/* Zero fill the data segment. */
	ldr	r2, =_sdata
	b	LoopFillZeroData
FillZeroData:
	movs	r3, #0
	str	r3, [r2]	/* ARMv6-M STR has no writeback form. */
	adds	r2, r2, #4
LoopFillZeroData:
	ldr	r3, =u_end
	cmp	r2, r3
	bcc	FillZeroData

	/* Copy data segment initializers from flash to SRAM. */
	movs	r1, #0
	b	LoopCopyDataInit
CopyDataInit:
	ldr	r3, =_sidata
	ldr	r3, [r3, r1]
	str	r3, [r0, r1]
	adds	r1, r1, #4
LoopCopyDataInit:
	ldr	r0, =_sdata
	ldr	r3, =_edata
	adds	r2, r0, r1
	cmp	r2, r3
	bcc	CopyDataInit

	/* Zero fill the bss segment. */
	ldr	r2, =_sbss
	b	LoopFillZeroBss
FillZeroBss:
	movs	r3, #0
	str	r3, [r2]	/* ARMv6-M STR has no writeback form. */
	adds	r2, r2, #4
LoopFillZeroBss:
	ldr	r3, =_ebss
	cmp	r2, r3
	bcc	FillZeroBss

system_init_and_main:
	bl	SystemInit
	bl	main

	/*
	 * Switch to the process stack and enter user space, still privileged.
	 *
	 * The MPU map mpu.c programmed before main() returned admits
	 * unprivileged fetches from the user window and the boot ROM only,
	 * so the instruction after a write that sets CONTROL.nPRIV must
	 * already be in the window: written here, in kernel flash, the
	 * following ISB faulted as the first unprivileged fetch. icode in
	 * locore.S, which init_main.c has copied to __user_data_start,
	 * therefore drops privilege as its first instructions, and this
	 * sequence selects the PSP and branches into it privileged.
	 */
setup_user_mode:
	cpsid	i		/* Disable interrupts. */

	/*
	 * ARMv6-M masks interrupts with PRIMASK alone, and CPSIE I below clears
	 * it. The Cortex-M4 original zeroed BASEPRI here to reach spl0.
	 */

	/* Switch SPSEL to Process Stack Pointer (PSP) for user space. */
	ldr	r0, =__user_data_end
	msr	PSP, r0		/* PSP is the user space stack pointer. */
	isb			/* Just to be safe. */

	mrs	r0, CONTROL
	movs	r1, #0x2	/* CONTROL[1] PSP; nPRIV stays clear here. */
	orrs	r0, r1		/* ARMv6-M ORRS takes register operands only. */

	cpsie	i		/* Enable interrupts; clears PRIMASK. */

	msr	CONTROL, r0	/* PSP is current sp; still privileged. */
	isb			/* CONTROL takes effect before the branch. */

	/* Run icode() in user space; its first instructions set nPRIV. */
	ldr	r0, =__user_data_start + 1	/* In Thumb must set lsbit. */
	mov	lr, r0		/* ARMv6-M LDR (literal) targets R0-R7 only. */
	bx	lr
	.size	Reset_Handler, . - Reset_Handler

	.section	.text.Default_Handler, "ax", %progbits
	.type	Default_Handler, %function
Default_Handler:
InfiniteLoop:
	b	InfiniteLoop
	.size	Default_Handler, . - Default_Handler

/*
 * The RP2040 vector table: ARMv6-M's 16 system entries followed by the
 * chip's 32 interrupts. ARMv6-M defines neither the MemManage, BusFault,
 * and UsageFault entries nor DebugMon, so those slots stay reserved.
 */
	.section	.isr_vector, "a", %progbits
	.type	g_pfnVectors, %object

g_pfnVectors:
	.word	_estack
	.word	Reset_Handler
	.word	NMI_Handler
	.word	HardFault_Handler
	.word	0			/* Reserved on ARMv6-M. */
	.word	0			/* Reserved on ARMv6-M. */
	.word	0			/* Reserved on ARMv6-M. */
	.word	0
	.word	0
	.word	0
	.word	0
	.word	SVC_Handler
	.word	0			/* Reserved on ARMv6-M. */
	.word	0
	.word	PendSV_Handler
	.word	SysTick_Handler

	/* RP2040 external interrupts, in hardware order. */
	.word	TIMER_IRQ_0_Handler
	.word	TIMER_IRQ_1_Handler
	.word	TIMER_IRQ_2_Handler
	.word	TIMER_IRQ_3_Handler
	.word	PWM_IRQ_WRAP_Handler
	.word	USBCTRL_IRQ_Handler
	.word	XIP_IRQ_Handler
	.word	PIO0_IRQ_0_Handler
	.word	PIO0_IRQ_1_Handler
	.word	PIO1_IRQ_0_Handler
	.word	PIO1_IRQ_1_Handler
	.word	DMA_IRQ_0_Handler
	.word	DMA_IRQ_1_Handler
	.word	IO_IRQ_BANK0_Handler
	.word	IO_IRQ_QSPI_Handler
	.word	SIO_IRQ_PROC0_Handler
	.word	SIO_IRQ_PROC1_Handler
	.word	CLOCKS_IRQ_Handler
	.word	SPI0_IRQ_Handler
	.word	SPI1_IRQ_Handler
	.word	UART0_IRQ_Handler
	.word	UART1_IRQ_Handler
	.word	ADC_IRQ_FIFO_Handler
	.word	I2C0_IRQ_Handler
	.word	I2C1_IRQ_Handler
	.word	RTC_IRQ_Handler
	.word	SPARE_IRQ_0_Handler
	.word	SPARE_IRQ_1_Handler
	.word	SPARE_IRQ_2_Handler
	.word	SPARE_IRQ_3_Handler
	.word	SPARE_IRQ_4_Handler
	.word	SPARE_IRQ_5_Handler

	.size	g_pfnVectors, . - g_pfnVectors

/*
 * Every handler is weak and aliases Default_Handler, so a driver defines the
 * one it needs and the rest resolve to the infinite loop above.
 */
	.macro	def_irq_handler handler_name
	.weak	\handler_name
	.thumb_set \handler_name, Default_Handler
	.endm

	def_irq_handler	NMI_Handler
	def_irq_handler	HardFault_Handler
	def_irq_handler	SVC_Handler
	def_irq_handler	PendSV_Handler
	def_irq_handler	SysTick_Handler

	def_irq_handler	TIMER_IRQ_0_Handler
	def_irq_handler	TIMER_IRQ_1_Handler
	def_irq_handler	TIMER_IRQ_2_Handler
	def_irq_handler	TIMER_IRQ_3_Handler
	def_irq_handler	PWM_IRQ_WRAP_Handler
	def_irq_handler	USBCTRL_IRQ_Handler
	def_irq_handler	XIP_IRQ_Handler
	def_irq_handler	PIO0_IRQ_0_Handler
	def_irq_handler	PIO0_IRQ_1_Handler
	def_irq_handler	PIO1_IRQ_0_Handler
	def_irq_handler	PIO1_IRQ_1_Handler
	def_irq_handler	DMA_IRQ_0_Handler
	def_irq_handler	DMA_IRQ_1_Handler
	def_irq_handler	IO_IRQ_BANK0_Handler
	def_irq_handler	IO_IRQ_QSPI_Handler
	def_irq_handler	SIO_IRQ_PROC0_Handler
	def_irq_handler	SIO_IRQ_PROC1_Handler
	def_irq_handler	CLOCKS_IRQ_Handler
	def_irq_handler	SPI0_IRQ_Handler
	def_irq_handler	SPI1_IRQ_Handler
	def_irq_handler	UART0_IRQ_Handler
	def_irq_handler	UART1_IRQ_Handler
	def_irq_handler	ADC_IRQ_FIFO_Handler
	def_irq_handler	I2C0_IRQ_Handler
	def_irq_handler	I2C1_IRQ_Handler
	def_irq_handler	RTC_IRQ_Handler
	def_irq_handler	SPARE_IRQ_0_Handler
	def_irq_handler	SPARE_IRQ_1_Handler
	def_irq_handler	SPARE_IRQ_2_Handler
	def_irq_handler	SPARE_IRQ_3_Handler
	def_irq_handler	SPARE_IRQ_4_Handler
	def_irq_handler	SPARE_IRQ_5_Handler
