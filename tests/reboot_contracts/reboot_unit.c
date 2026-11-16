/* Copyright (c) 2026 GizmoBSD
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
 * The translation unit under test: sbin/reboot/reboot.c with its system
 * calls redirected to the harness's recorders. Every header reboot.c
 * includes is included here first, so each system prototype keeps the C
 * library's own spelling, including any asm-label alias the C library
 * attaches (macOS declares syslog() as syslog$DARWIN_EXTSN). The renames
 * follow, and apply to the calls alone; the recorders are declared plainly
 * below. sdk_gap.sh checks that this list covers every include of reboot.c.
 * REBOOT_SOURCE is the quoted path of the source or of a mutant of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <pwd.h>
#include <sysexits.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syslog.h>
#include <sys/file.h>
#include <sys/reboot.h>
#include <sys/signal.h>
#include <utmp.h>
#include <paths.h>

int test_reboot(int);
int test_kill(pid_t, int);
int test_sync(void);
unsigned test_sleep(unsigned);
uid_t test_geteuid(void);
int test_open(const char *, int, ...);
void test_openlog(const char *, int, int);
void test_syslog(int, const char *, ...);
char *test_getlogin(void);

#define main discobsd_reboot_main
#define reboot test_reboot
#define kill test_kill
#define sync test_sync
#define sleep test_sleep
#define geteuid test_geteuid
#define open test_open
#define openlog test_openlog
#define syslog test_syslog
#define getlogin test_getlogin

#include REBOOT_SOURCE
