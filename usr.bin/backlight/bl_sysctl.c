/*
 * Copyright (c) 2026 GizmoBSD
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The levels of backlight.h through sysctl(3). The kernel's handler,
 * picocalc_sysctl() in sys/arch/rp2040/dev/picokbd.c, reads the controller's
 * register only when the caller supplies an old-value buffer, and writes it
 * when the caller supplies a new value. bl_set() passes NULL for the
 * old-value pointer, so a write costs one register exchange instead of two.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/picocalcvar.h>
#include <errno.h>
#include <unistd.h>

#include "backlight.h"

static void
node(int light, int *mib)
{
	mib[0] = CTL_MACHDEP;
	mib[1] = CPU_PICOCALC;
	mib[2] = light == BL_KBD ? CPU_PICOCALC_KBD_BACKLIGHT :
	    CPU_PICOCALC_LCD_BACKLIGHT;
}

static int
failed(void)
{
	return errno != 0 ? errno : EIO;
}

int
bl_get(int light, int *level)
{
	int mib[3];
	size_t len = sizeof *level;

	node(light, mib);
	if (sysctl(mib, 3, level, &len, NULL, 0) < 0)
		return failed();
	return len == sizeof *level ? 0 : EIO;
}

int
bl_set(int light, int level)
{
	int mib[3];

	node(light, mib);
	return sysctl(mib, 3, NULL, NULL, &level, sizeof level) < 0 ?
	    failed() : 0;
}

int
bl_root(void)
{
	return getuid() == 0;
}
