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
 * Drives one err-family entry point selected by argv[1] (0-7 in the order
 * err, errx, warn, warnx, verr, verrx, vwarn, vwarnx) with a formatted or,
 * when a second argument is present, a null message. tests/libc_contracts/
 * err_test.py checks the stderr text and the exit or return status.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <err.h>

extern char *__progname;

static void
forward(int mode, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    switch (mode) {
    case 4: verr(23, fmt, ap); break;
    case 5: verrx(24, fmt, ap); break;
    case 6: vwarn(fmt, ap); break;
    case 7: vwarnx(fmt, ap); break;
    }
    va_end(ap);
}

int
main(int argc, char **argv)
{
    int mode = atoi(argv[1]);
    const char *fmt = argc > 2 ? NULL : "%d %s %.2f";

    __progname = "err-probe";
    errno = ENOENT;
    switch (mode) {
    case 0: err(21, fmt, 37, "word", 1.25); break;
    case 1: errx(22, fmt, 37, "word", 1.25); break;
    case 2: warn(fmt, 37, "word", 1.25); break;
    case 3: warnx(fmt, 37, "word", 1.25); break;
    default: forward(mode, fmt, 37, "word", 1.25); break;
    }
    /* Returning warnings must leave their caller able to continue. */
    return 19;
}
