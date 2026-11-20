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
; mbr.asm: the first sector of a hard disk image for i86(1), in 8086
; instructions. It moves itself to 0:0600, finds the active entry of the
; partition table, loads that partition's first sector to 0:7C00 and
; starts it with DL holding the drive and SI the entry, as DOS boot
; sectors expect. The partition sectors SvarDOS ships use 80386
; instructions and stop an 8086 or 80286.
;
;	nasm -f bin -o mbr.bin mbr.asm
;
; mbr.hexdump is that output in hexadecimal; mkdosdisk.sh writes it.

	bits 16
	cpu 8086
	org 0x600

	cli
	xor ax, ax
	mov ss, ax
	mov sp, 0x7c00
	mov ds, ax
	mov es, ax
	sti
	cld
	mov si, 0x7c00
	mov di, 0x600
	mov cx, 256
	rep movsw
	jmp 0:moved

moved:	mov si, 0x600 + 446
	mov cx, 4
.find:	test byte [si], 0x80
	jnz .load
	add si, 16
	loop .find
	mov si, m_none
	jmp say

.load:	mov dh, [si + 1]
	mov cx, [si + 2]
	mov ax, 0x0201
	mov bx, 0x7c00
	int 0x13
	jc .bad
	cmp word [0x7dfe], 0xaa55
	jne .bad
	jmp 0:0x7c00
.bad:	mov si, m_read

say:	lodsb
	or al, al
	jz .key
	mov ah, 0x0e
	int 0x10
	jmp say
.key:	xor ah, ah
	int 0x16
	int 0x18

m_none:	db "No active partition", 0
m_read:	db "Cannot read the active partition", 0

	times 440 - ($ - $$) db 0
