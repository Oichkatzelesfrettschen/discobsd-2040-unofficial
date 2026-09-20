/*
 * Known-bad getty mode application for calibrating the contract gate.
 * The fixture drops the local mode word instead of issuing TIOCLSET.
 */

#include <sgtty.h>

#include "gettytab.h"

struct sgttyb tmode;
struct tchars tc;
struct ltchars ltc;
char hostname[32];

void
splitflags(long allflags, struct sgttyb *terminal_mode, int *local_modes)
{
	terminal_mode->sg_flags = (short)(allflags & 0xffff);
	*local_modes = (int)(allflags >> 16);
}

void
applymode(long allflags, struct sgttyb *terminal_mode, int lowflags)
{
	int local_modes;

	splitflags(allflags, terminal_mode, &local_modes);
	terminal_mode->sg_flags |= (short)lowflags;
	(void)local_modes;
}

void
resolveparity(void)
{
	if (!(OPset || EPset || APset))
		return;
	if (!OPset)
		OP = 0;
	if (!EPset)
		EP = 0;
	if (!APset)
		AP = 0;
	APset = OPset = EPset = 1;
}
