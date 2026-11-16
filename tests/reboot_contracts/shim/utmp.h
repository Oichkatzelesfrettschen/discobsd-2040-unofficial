/*
 * Copyright (c) 2026 GizmoBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Host shim for the 2.11BSD utmp record that markdown() fills in.
 */
#ifndef REBOOT_SHIM_UTMP_H
#define REBOOT_SHIM_UTMP_H

/*
 * The macOS SDK keeps _PATH_WTMP in its own <utmp.h>, which this shim
 * replaces, so supply the path where <paths.h> lacks it.
 * REBOOT_SHIM_NO_WTMP_FALLBACK removes the fallback for sdk_gap.sh.
 */
#include <paths.h>
#if !defined(_PATH_WTMP) && !defined(REBOOT_SHIM_NO_WTMP_FALLBACK)
#define _PATH_WTMP "/var/log/wtmp"
#endif

typedef long utime32_t;

struct utmp {
	char	ut_line[8];
	char	ut_name[16];	/* the target record has 8; wider here so the host
				   compiler accepts the unterminated copy */
	char	ut_host[16];
	utime32_t ut_time;
};

#endif
