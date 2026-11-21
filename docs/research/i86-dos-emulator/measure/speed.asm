; Copyright (c) 2026 GizmoBSD
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
; 1. Redistributions of source code must retain the above copyright notice,
;    this list of conditions and the following disclaimer.
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
; 3. Neither the name of the copyright holder nor the names of its contributors
;    may be used to endorse or promote products derived from this software
;    without specific prior written permission.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
; ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
; LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
; CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
; SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
; INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
; CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
; ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
; POSSIBILITY OF SUCH DAMAGE.
;
; speed: a boot sector that runs a fixed instruction mix with interrupts
; off, prints BENCH DONE and halts. Under i86 -k, a processor halted with
; interrupts off ends the session, so the run time of
;
;	i86 -d -q -v -k x -a speed.img
;
; is the time of 11,000,309 guest instructions (OUTER 100) plus start-up
; and exit. Built with OUTER 1 (110,012 instructions) the same command
; measures start-up and exit alone. Assemble with
;
;	nasm -f bin -D OUTER=100 -o speed.bin speed.asm
;
; and pad the 512 bytes to any floppy size (board-speed-pico.txt used 4096).
; The loop body is eleven instructions: a load, an add, a store with a
; 16-bit displacement, three register operations, an increment, a mask, a
; near call and its return, and LOOP.
bits 16
org 0x7c00
start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	cld
	xor bx, bx
	mov dx, OUTER
outer:
	mov cx, 10000
inner:
	mov ax, [bx+buf]
	add ax, cx
	mov [bx+buf+2], ax
	xor bp, ax
	shl ax, 1
	adc bp, 0
	inc bx
	and bx, 0xff
	call sub1
	loop inner
	dec dx
	jnz outer
	mov si, msg
.p:	lodsb
	or al, al
	jz .h
	mov ah, 0x0e
	int 0x10
	jmp .p
.h:	cli
	hlt
	jmp .h
sub1:	ret
msg:	db "BENCH DONE", 0
buf:	times 260 db 0
	times 510-($-$$) db 0
	dw 0xaa55
