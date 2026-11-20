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
; The guest of the mouse test (tests/check.py): a boot program that drives
; COM1 as a serial mouse driver does and prints what it reads, one token a
; line with no blank in it. Assemble with
;
;	nasm -f bin -o mouse.bin mouse.asm
;
; mouse.hexdump is the assembled program, 32 bytes a line. The setup prints
; the data area's COM1 address and serial port count, the scratch
; register, the loopback status and echo, the line status after two ticks
; of loopback with RTS set (nothing from the mouse), the line status after
; the mouse's power comes on and two ticks later, the interrupts taken
; before and after OUT2 (set by one word written to ports 3FBh and 3FCh),
; the modem control and line status read as one word, the identification
; register in the handler and the identification byte. Then "ready", then
; a line for each report (p, the buttons, X and Y as bytes) and for each
; key (k, its code, and the most bytes the handler took in one timer
; tick).

	bits 16
	cpu 8086
	org 0x7c00

COM	equ 0x3f8
RING	equ 64

start:	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	mov ax, 0x0202			; the two sectors after this one
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

; si: text ending in 0.
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

hex4:	xchg al, ah
	call hex2
	xchg al, ah
	jmp hex2

crlf:	push ax
	mov al, 13
	call putc
	mov al, 10
	call putc
	pop ax
	ret

; "label:XX" for the label at si and the value in al.
say2:	push ax
	call puts
	pop ax
	call hex2
	jmp crlf

; The same for the register at port dx.
sayin:	in al, dx
	jmp say2

; cx timer ticks.
ticks:	sti
.next:	mov ax, [0x46c]
.wait:	hlt
	cmp ax, [0x46c]
	je .wait
	loop .next
	ret

; The next byte from the ring.
get:	push di
	mov di, [tail]
	mov al, [ring + di]
	inc di
	and di, RING - 1
	mov [tail], di
	pop di
	ret

	times 510 - ($ - $$) db 0
	dw 0xaa55

main:	mov si, s_com
	call puts
	mov ax, [0x400]
	call hex4
	call crlf
	mov si, s_ports
	call puts
	mov ax, [0x410]
	and ax, 0x0e00
	call hex4
	call crlf
	mov dx, COM + 3			; 1200 bit/s, seven bits, one stop bit
	mov al, 0x80
	out dx, al
	mov dx, COM
	mov al, 0x60
	out dx, al
	inc dx
	xor al, al
	out dx, al
	mov dx, COM + 3
	mov al, 0x02
	out dx, al
	mov dx, COM + 7
	mov al, 0x5a
	out dx, al
	mov si, s_scr
	call sayin
	mov dx, COM + 4			; loopback with RTS and OUT2
	mov al, 0x1a
	out dx, al
	mov dx, COM + 6
	mov si, s_msr
	call sayin
	mov dx, COM
	mov al, 0xa5
	out dx, al
	mov dx, COM + 5
	mov si, s_lsr
	call sayin
	mov dx, COM
	mov si, s_echo
	call sayin
	mov cx, 2			; loopback keeps the mouse unpowered
	call ticks
	mov dx, COM + 5
	mov si, s_quiet
	call sayin
	mov dx, COM + 4			; power off
	xor al, al
	out dx, al
	mov word [0x0c * 4], irq4
	mov word [0x0c * 4 + 2], 0
	in al, 0x21
	and al, 0xef
	out 0x21, al
	mov dx, COM + 1			; receive interrupt, OUT2 still off
	mov al, 1
	out dx, al
	mov dx, COM + 4			; power on
	mov al, 3
	out dx, al
	mov dx, COM + 5
	mov si, s_lsr
	call sayin
	mov cx, 2
	call ticks
	mov dx, COM + 5
	mov si, s_lsr
	call sayin
	mov al, [irqs]
	mov si, s_irqs
	call say2
	mov dx, COM + 3			; OUT2: the waiting byte interrupts.
	mov ax, 0x0b02			; One word, line and modem control,
	out dx, ax			; as CTMOUSE writes them.
	mov cx, 1
	call ticks
	mov dx, COM + 4			; modem control and line status
	in ax, dx
	mov si, s_word
	push ax
	call puts
	pop ax
	call hex4
	call crlf
	mov al, [irqs]
	mov si, s_irqs
	call say2
	mov al, [iir]
	mov si, s_iir
	call say2
	call get
	mov si, s_id
	call say2
	mov si, s_ready
	call puts
	call crlf
next:	mov ax, [head]
	sub ax, [tail]
	and ax, RING - 1
	cmp ax, 3
	jb .keys
	call get
	test al, 0x40
	jz next
	mov bl, al
	call get
	mov cl, al
	call get
	mov ch, al
	mov al, 'p'
	call putc
	mov al, ':'
	call putc
	mov al, '.'
	test bl, 0x20
	jz .left
	mov al, 'L'
.left:	call putc
	mov al, '.'
	test bl, 0x10
	jz .right
	mov al, 'R'
.right:	call putc
	mov al, ':'
	call putc
	mov al, bl			; X: two high bits, six low
	ror al, 1
	ror al, 1
	and al, 0xc0
	or al, cl
	call hex2
	mov al, ':'
	call putc
	mov al, bl			; Y
	shl al, 1
	shl al, 1
	shl al, 1
	shl al, 1
	and al, 0xc0
	or al, ch
	call hex2
	call crlf
	jmp next
.keys:	mov ah, 1
	int 0x16
	jz .idle
	xor ax, ax
	int 0x16
	mov si, s_key
	push ax
	call puts
	pop ax
	call hex4
	mov al, ':'
	call putc
	mov al, [rate]
	call hex2
	call crlf
	jmp next
.idle:	sti
	hlt
	jmp next

; Line 4: the identification register, the byte into the ring, and the
; most bytes taken in one timer tick.
irq4:	push ax
	push dx
	push di
	push ds
	xor ax, ax
	mov ds, ax
	mov dx, COM + 2
	in al, dx
	mov [iir], al
	mov dx, COM
	in al, dx
	mov di, [head]
	mov [ring + di], al
	inc di
	and di, RING - 1
	mov [head], di
	inc byte [irqs]
	mov ax, [0x46c]
	cmp ax, [rtick]
	je .same
	mov [rtick], ax
	mov byte [rcount], 0
.same:	inc byte [rcount]
	mov al, [rcount]
	cmp al, [rate]
	jbe .eoi
	mov [rate], al
.eoi:	mov al, 0x20
	out 0x20, al
	pop ds
	pop di
	pop dx
	pop ax
	iret

s_com:	db "com:", 0
s_ports: db "ports:", 0
s_scr:	db "scr:", 0
s_msr:	db "msr:", 0
s_lsr:	db "lsr:", 0
s_echo:	db "echo:", 0
s_irqs:	db "irqs:", 0
s_iir:	db "iir:", 0
s_id:	db "id:", 0
s_ready: db "ready", 0
s_quiet: db "quiet:", 0
s_word:	db "word:", 0
s_key:	db "k:", 0

head:	dw 0
tail:	dw 0
rtick:	dw 0
irqs:	db 0
iir:	db 0
rate:	db 0
rcount:	db 0
ring:	times RING db 0

	times 1536 - ($ - $$) db 0
