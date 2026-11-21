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
; The guest of the timer test (tests/check.py): a boot program that drives
; the 8254 at ports 40h to 43h and prints one token a line. Assemble with
;
;	nasm -f bin -o pit.bin pit.asm
;
; pit.hexdump is the assembled program, 32 bytes a line. It prints how far
; counter 0 moved between two latched reads 100 loop turns apart (dn),
; the interrupts counter 0 raised at 1 kHz (mode 2, count 1193) while
; counter 2 counted 65535 clocks in mode 0 with its gate open, polled
; through bit 5 of port 61h (irq), counter 2's read-back status then
; (st2), how often bit 4 of port 61h changed in 64 reads (rf), and, with
; counter 0 back at 65536 and the firmware's handler, that the tick count
; at 40:6Ch moved (tick).

	bits 16
	cpu 8086
	org 0x7c00

start:	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	mov ax, 0x0201			; the sector after this one
	mov cx, 0x0002
	xor dh, dh
	mov bx, 0x7e00
	int 0x13
	jc dead
	jmp main
dead:	cli
	hlt
	jmp dead

putc:	push ax
	push bx
	mov ah, 0x0e
	mov bx, 7
	int 0x10
	pop bx
	pop ax
	ret

puts:	lodsb
	test al, al
	jz .done
	call putc
	jmp puts
.done:	ret

hex2:	push ax
	push cx
	push ax
	mov cl, 4
	shr al, cl
	call .digit
	pop ax
	call .digit
	pop cx
	pop ax
	ret
.digit:	and al, 15
	add al, '0'
	cmp al, '9'
	jbe .out
	add al, 7
.out:	jmp putc

; "label:XXXX" for the label at si and ax, then a line.
say4:	push ax
	call puts
	pop ax
	xchg al, ah
	call hex2
	xchg al, ah
	call hex2
crlf:	mov al, 13
	call putc
	mov al, 10
	jmp putc

; ax: counter 0, latched.
latch0:	mov al, 0x00
	out 0x43, al
	in al, 0x40
	mov ah, al
	in al, 0x40
	xchg al, ah
	ret

ticks:	push ax
	inc word [cs:nirq]
	mov al, 0x20
	out 0x20, al
	pop ax
	iret

nirq:	dw 0

	times 510 - ($ - $$) db 0
	dw 0xaa55

main:	call latch0
	mov bx, ax
	mov cx, 100
.spin:	loop .spin
	call latch0
	sub bx, ax
	mov ax, bx
	mov si, s_dn
	call say4
	; counter 0 at 1 kHz into a handler of ours
	cli
	push word [8 * 4]
	push word [8 * 4 + 2]
	mov word [8 * 4], ticks
	mov word [8 * 4 + 2], 0
	mov al, 0x34
	out 0x43, al
	mov al, 0xa9
	out 0x40, al
	mov al, 0x04
	out 0x40, al
	; counter 2: 65535 clocks in mode 0, gate open, speaker off
	in al, 0x61
	and al, 0xfc
	or al, 1
	out 0x61, al
	mov al, 0xb0
	out 0x43, al
	mov al, 0xff
	out 0x42, al
	out 0x42, al
	mov word [nirq], 0
	sti
.wait:	in al, 0x61
	test al, 0x20
	jz .wait
	cli
	mov ax, [nirq]
	mov si, s_irq
	call say4
	mov al, 0xe8			; read back counter 2's status
	out 0x43, al
	in al, 0x42
	xor ah, ah
	mov si, s_st2
	call say4
	; bit 4 of port 61h over 64 reads
	xor bx, bx
	in al, 0x61
	mov dl, al
	mov cx, 64
.rf:	in al, 0x61
	xor dl, al
	test dl, 0x10
	jz .same
	inc bx
.same:	mov dl, al
	loop .rf
	mov ax, bx
	mov si, s_rf
	call say4
	; counter 0 back at 65536 in mode 3, the firmware's handler back
	mov al, 0x36
	out 0x43, al
	xor al, al
	out 0x40, al
	out 0x40, al
	pop word [8 * 4 + 2]
	pop word [8 * 4]
	mov ax, [0x46c]
	sti
.tick:	hlt
	cmp ax, [0x46c]
	je .tick
	cli
	mov si, s_tick
	call puts
	call crlf
	mov si, s_done
	call puts
	call crlf
	jmp dead

s_dn:	db "dn:", 0
s_irq:	db "irq:", 0
s_st2:	db "st2:", 0
s_rf:	db "rf:", 0
s_tick:	db "tick:ok", 0
s_done:	db "done", 0

	times 1024 - ($ - $$) db 0
