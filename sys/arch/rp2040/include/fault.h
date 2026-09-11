/*
 * Copyright (c) 2023 Christopher Hettrick <chris@structfoo.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_MACHINE_FAULT_H_
#define	_MACHINE_FAULT_H_

/*
 * ARMv6-M fault reporting.
 *
 * ARMv7-M raises four fault exceptions and describes each through a status
 * register: HFSR for HardFault, and CFSR split into MMFSR, BFSR, and UFSR,
 * with MMFAR and BFAR holding the offending address. The STM32 edition of
 * this header decodes all of them.
 *
 * ARMv6-M has none of that. It raises one fault exception, HardFault, and
 * defines no fault status or fault address register at all, so a fault here
 * carries no cause code and no faulting address. Losing that diagnosis is a
 * real capability loss rather than a translation, and there is nothing on the
 * RP2040 to recover it from.
 *
 * What remains is the exception stack frame the hardware pushes, which holds
 * the return address and xPSR of the faulting context. See <machine/frame.h>.
 * A fault handler reports that and stops.
 */

/*
 * ICSR VECTACTIVE identifies the running exception, which is the only
 * discrimination left: a fault is always exception number 3, HardFault.
 */
#define	RP2040_SCB_ICSR		0xe000ed04UL
#define	SCB_ICSR_VECTACTIVE	0x0000003fUL

#define	EXC_HARDFAULT		3	/* The only fault ARMv6-M raises. */

#endif	/* !_MACHINE_FAULT_H_ */
