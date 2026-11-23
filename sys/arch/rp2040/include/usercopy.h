/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
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
 */

#ifndef _RP2040_USERCOPY_H_
#define _RP2040_USERCOPY_H_

/*
 * Return nonzero when [start, start + length) lies inside [lower, upper).
 * Subtraction after ordered-bound checks avoids constructing a wrapped end.
 * The empty span at upper is valid; callers decide whether an empty transfer
 * also accepts addresses outside the modeled interval.
 */
static inline int
rp2040_user_span_contains(unsigned int lower, unsigned int upper,
    unsigned int start, unsigned int length)
{
	if (upper < lower || start < lower || start > upper)
		return 0;

	return length <= upper - start;
}

/*
 * Return nonzero when [start, start + length) lies inside the PMSAv6 region
 * of 2^log2 bytes at base and touches only subregions that srd leaves
 * enabled (one disable bit per eighth, RASR.SRD, ARMv6-M ARM B3.5.9). log2
 * runs from 8 to 31, so the shifts stay below the width of the type, and an
 * empty span touches zero subregions. A region ending at 2^32 is outside the
 * model and rejected.
 */
static inline int
rp2040_user_region_span_contains(unsigned int base, unsigned int log2,
    unsigned int srd, unsigned int start, unsigned int length)
{
	unsigned int size, eighth, first, last;

	if (log2 < 8 || log2 > 31)
		return 0;
	size = 1U << log2;
	if (base > ~0U - size ||
	    !rp2040_user_span_contains(base, base + size, start, length))
		return 0;
	if (length == 0)
		return 1;
	eighth = size >> 3;
	first = (start - base) / eighth;
	last = (start - base + (length - 1U)) / eighth;
	for (; first <= last; first++)
		if (srd & (1U << first))
			return 0;
	return 1;
}

#endif	/* !_RP2040_USERCOPY_H_ */
