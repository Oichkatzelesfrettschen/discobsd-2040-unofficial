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

.syntax unified
.thumb
.text
.global _start
.thumb_func
_start:
    push {r4,lr}
    ldr r0,=__xip_libc_state
    ldr r3,=0x10020000
    ldr r1,[r3,#28]
    adds r3,r3,r1
    adds r3,#1
    blx r3
    mov r9,r0
    movs r0,#0
    bl sbrk
    ldr r1,=_end
    cmp r0,r1
    bne wrong
    bl rand
    ldr r1,=16838
    cmp r0,r1
    bne wrong
    svc #2
    bcs wrong
    cmp r0,#0
    beq child
    mov r4,r0
    sub sp,#8
    mov r1,sp
    movs r2,#0
    movs r3,#0
    svc #7
    bcs wrong
    cmp r0,r4
    bne wrong
    ldr r0,[sp,#0]
    ldr r1,=9472
    cmp r0,r1
    bne wrong
    add sp,#8
    bl rand
    ldr r1,=5758
    cmp r0,r1
    bne wrong
    movs r0,#1
    ldr r1,=message
    movs r2,#8
    svc #4
    movs r0,#0
    svc #1
child:
    movs r4,#79
    ldr r0,=badraw
    bl rejected
    ldr r0,=badpacked
    bl rejected
    ldr r0,=shortreq
    bl rejected
    ldr r0,=badstate
    bl rejected
    ldr r0,=shortpack
    bl rejected
    movs r4,#12
    ldr r0,=fullraw
    bl rejected
    ldr r0,=fullpack
    bl rejected
    bl rand
    ldr r1,=5758
    cmp r0,r1
    bne wrong
    movs r0,#37
    svc #1
rejected:
    ldr r1,=args
    movs r2,#0
    svc #59
    bcc wrong
    cmp r0,r4
    bne wrong
    bx lr
wrong:
    movs r0,#1
    svc #1
    .pool
message:
    .ascii "XIP-FORK"
badraw:
    .asciz "/badraw"
badpacked:
    .asciz "/badpacked"
shortreq:
    .asciz "/shortreq"
badstate:
    .asciz "/badstate"
shortpack:
    .asciz "/shortpack"
fullraw:
    .asciz "/fullraw"
fullpack:
    .asciz "/fullpack"
    .align 2
args:
    .word badraw
    .word 0
