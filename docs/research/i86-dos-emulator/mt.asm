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
; MT.COM, the probe of ctmouse-host.txt: the UART's modem control,
; interrupt enable, line control and line status registers and the
; interrupt controller's mask, then interrupt 33h's reset (buttons), the
; registers again, and the buttons, X and Y each time one of them changes.
; q leaves. Assemble with
;
;	nasm -f bin -o MT.COM mt.asm
;
	org 0x100
	call regs
	xor ax, ax
	int 0x33
	jmp after
regs:	mov si, s_regs			; MCR, IER, LCR, LSR and the PIC's mask
	call say
	mov dx, 0x3fc
	in al, dx
	call hex2
	mov dx, 0x3f9
	in al, dx
	call hex2
	mov dx, 0x3fb
	in al, dx
	call hex2
	mov dx, 0x3fd
	in al, dx
	call hex2
	in al, 0x21
	call hex2
	jmp nl
after:
	mov si, s_reset
	call say
	mov ax, bx
	call hex4
	call nl
	call regs
.loop:	mov ax, 3
	int 0x33
	cmp bx, [lb]
	jne .show
	cmp cx, [lx]
	jne .show
	cmp dx, [ly]
	je .key
.show:	mov [lb], bx
	mov [lx], cx
	mov [ly], dx
	mov si, s_b
	call say
	mov ax, [lb]
	call hex4
	mov si, s_x
	call say
	mov ax, [lx]
	call hex4
	mov si, s_y
	call say
	mov ax, [ly]
	call hex4
	call nl
.key:	mov ah, 1
	int 0x16
	jz .loop
	xor ax, ax
	int 0x16
	cmp al, 'q'
	jne .loop
	int 0x20

; si: text ending in 0; ax kept.
say:	push ax
.next:	lodsb
	test al, al
	jz .done
	mov dl, al
	mov ah, 2
	int 0x21
	jmp .next
.done:	pop ax
	ret

hex4:	push ax
	mov al, ah
	call hex2
	pop ax
hex2:	push ax
	shr al, 1
	shr al, 1
	shr al, 1
	shr al, 1
	call hex1
	pop ax
hex1:	push ax
	and al, 15
	add al, '0'
	cmp al, '9'
	jbe .out
	add al, 7
.out:	mov dl, al
	mov ah, 2
	int 0x21
	pop ax
	ret

nl:	mov si, s_nl
	jmp say

s_reset: db "reset:", 0
s_regs:	db "regs:", 0
s_b:	db "b:", 0
s_x:	db ",x:", 0
s_y:	db ",y:", 0
s_nl:	db 13, 10, 0
lb:	dw 0xffff
lx:	dw 0xffff
ly:	dw 0xffff
