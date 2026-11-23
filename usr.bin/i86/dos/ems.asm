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
;
; EMS.SYS, the DOS device driver for i86's expanded memory (ems.c). It
; names the device EMMXXXX0, by which programs find an expanded memory
; manager, answers every request with "done", and points interrupt 67h at
; the firmware's own service. It installs only on i86 with expanded
; memory (-e): the firmware's entry for interrupt 67h must hold the trap
; 0F FF 67 and answer function 40h with status 0. Assemble with
;
;	nasm -f bin -o EMS.SYS ems.asm
;
; ems.hexdump is the assembled driver, 32 bytes a line. CONFIG.SYS loads it
; with DEVICE=C:\EMS.SYS.

	bits 16
	cpu 8086
	org 0

TRAP	equ 0xf000 + 0x67 * 4		; the firmware's entry in F000

header:	dd -1
	dw 0x8000			; a character device
	dw strategy
	dw interrupt
	db "EMMXXXX0"

request: dd 0

strategy:
	mov [cs:request], bx
	mov [cs:request + 2], es
	retf

interrupt:
	push ax
	push bx
	push ds
	lds bx, [cs:request]
	cmp byte [bx + 2], 0		; initialize
	jne .done
	call init
	jmp .out
.done:	mov word [bx + 3], 0x0100	; done, no error
.out:	pop ds
	pop bx
	pop ax
	retf

int67:	jmp 0xf000:TRAP

resident:

; ds:bx is the request header.
init:	push cx
	push dx
	push si
	push es
	mov ax, 0xf000
	mov es, ax
	cmp word [es:TRAP], 0xff0f
	jne .none
	cmp byte [es:TRAP + 2], 0x67
	jne .none
	mov ah, 0x40
	pushf
	call 0xf000:TRAP
	test ah, ah
	jnz .none
	xor ax, ax
	mov es, ax
	cli
	mov word [es:0x67 * 4], int67
	mov [es:0x67 * 4 + 2], cs
	sti
	push bx				; bx: the request header
	mov ah, 0x42			; dx: 16 KB pages in all
	int 0x67
	pop bx
	mov si, s_have
	call say
	mov ax, dx
	mov cl, 4
	shl ax, cl			; kilobytes
	call decimal
	mov si, s_kb
	call say
	mov word [bx + 0x0e], resident
	jmp .end
.none:	mov si, s_none
	call say
	mov word [bx + 0x0e], 0		; nothing stays
.end:	mov [bx + 0x10], cs
	mov word [bx + 3], 0x0100
	pop es
	pop si
	pop dx
	pop cx
	ret

; The text at cs:si, ending in 0, through DOS.
say:	push ax
	push dx
.next:	mov dl, [cs:si]
	inc si
	test dl, dl
	jz .end
	mov ah, 2
	int 0x21
	jmp .next
.end:	pop dx
	pop ax
	ret

; ax in decimal.
decimal:
	push ax
	push cx
	push dx
	xor cx, cx
.div:	xor dx, dx
	push bx
	mov bx, 10
	div bx
	pop bx
	push dx
	inc cx
	test ax, ax
	jnz .div
.put:	pop dx
	add dl, '0'
	mov ah, 2
	int 0x21
	loop .put
	pop dx
	pop cx
	pop ax
	ret

s_have:	db "i86 expanded memory: ", 0
s_kb:	db " KB", 13, 10, 0
s_none:	db "i86 expanded memory: none (start i86 with -e)", 13, 10, 0
