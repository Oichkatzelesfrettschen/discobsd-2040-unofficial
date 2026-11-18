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

/* The SD-root kernel keeps raw swap and every SRAM region at their
 * standard addresses. check-picocalc-sd-layout compares both maps.
 */
MEMORY {
    BOOT2   (rx)  : ORIGIN = 0x10000000, LENGTH = 256
    FLASH   (rx)  : ORIGIN = 0x10000100, LENGTH = 160K - 256
    FSFLASH (r)   : ORIGIN = 0x10028000, LENGTH = 1504K
    SWAPFLASH (r) : ORIGIN = 0x101A0000, LENGTH = 384K
    USERRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
    RAM     (rw!x): ORIGIN = 0x20030000, LENGTH = 58K
    U0AREA  (rw!x): ORIGIN = 0x2003E800, LENGTH = 3K
    UAREA   (rw!x): ORIGIN = 0x2003F400, LENGTH = 3K
    SCRATCH (rw!x): ORIGIN = 0x20040000, LENGTH = 8K
}

BOARD_ALIGN = 256;
BOARD_MAX = 256;
