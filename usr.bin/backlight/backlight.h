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
 * The interface between backlight.c and the place its levels live:
 * bl_sysctl.c on the target, tests/bl_fixture.c on the host.
 */
#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#define BL_LCD		0	/* the screen: machdep.picocalc.lcd_backlight */
#define BL_KBD		1	/* the keys: machdep.picocalc.kbd_backlight */
#define BL_LEVEL_MAX	240	/* the highest level the kernel passes on */

/* The boot level /etc/rc reads; backlight -s writes it. */
#ifndef BACKLIGHT_FILE
#define BACKLIGHT_FILE	"/etc/backlight"
#endif

/* The level the keyboard controller keeps for light; 0 or an errno. */
int	bl_get(int light, int *level);
/* Ask the controller for level 0..BL_LEVEL_MAX; 0 or an errno. */
int	bl_set(int light, int level);
/* Nonzero when the real user is root. */
int	bl_root(void);

#endif /* !BACKLIGHT_H */
