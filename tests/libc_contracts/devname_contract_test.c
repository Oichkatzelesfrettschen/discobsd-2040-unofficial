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
 * devname() contract: the name it returns stays valid after the directory
 * is closed, the requested file type decides between a character and a
 * block device that share a number, the longest entry name fits, and an
 * unmatched number or an unreadable /dev yields "??". Every call closes
 * the directory it opened.
 */
#include <stdio.h>
#include <string.h>

#include "devname_shim.h"

#define	LONGEST_NAME \
	"ttyabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh"

static unsigned int checks;
static unsigned int failures;

static void
check(int condition, const char *description)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "devname: %s\n", description);
		failures++;
	}
}

static void
expect(int dev, int character, const char *want, const char *description)
{
	const char *got = db_devname(dev, devname_shim_type(character));

	check(strcmp(got, want) == 0, description);
	check(devname_shim_open_dirs() == 0, "the directory stays open");
}

int
main(void)
{
	check(strlen(LONGEST_NAME) == 63, "the long name has MAXNAMLEN bytes");

	devname_shim_reset(0);
	devname_shim_add("sd0", 0, 0x0000);
	devname_shim_add("console", 1, 0x0000);
	devname_shim_add("tty0", 1, 0x0101);
	devname_shim_add(LONGEST_NAME, 1, 0x0202);
	expect(0x0101, 1, "tty0", "the name does not survive closedir");
	expect(0x0000, 1, "console",
	    "a block device answers for a character device");
	expect(0x0000, 0, "sd0",
	    "a character device answers for a block device");
	expect(0x0202, 1, LONGEST_NAME, "the longest name is cut");
	expect(0x0303, 1, "??", "an unmatched number names a device");

	devname_shim_reset(1);
	expect(0x0101, 1, "??", "an unreadable /dev names a device");

	if (failures != 0) {
		fprintf(stderr, "devname: %u of %u checks failed\n",
		    failures, checks);
		return (1);
	}
	printf("devname contract: %u checks passed\n", checks);
	return (0);
}
