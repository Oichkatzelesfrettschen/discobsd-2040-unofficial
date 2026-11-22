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
; The guest of tests/check.py: a boot sector and the sectors it loads,
; which exercise the machine of pc.c through the firmware interface and
; print one line per result. Assemble with
;
;	nasm -f bin -o guest.bin guest.asm
;
; guest.hexdump is that output in hexadecimal; check.py compares the two when
; nasm is installed.
;
; The guest of the panel display test (tests/check.py): a boot program that
; reads a digit from the keyboard and draws that stage through display
; memory, the firmware's video services and the adapter's ports, then
; stops. check.py computes the frame each stage must leave on the panel.
;
;	nasm -f bin -o panel.bin panel.asm
;
; panel.hexdump is the assembled program, 32 bytes a line.

	bits 16
	cpu 8086
	org 0x7c00

start:	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	mov ax, 0x0202			; the two sectors after this one
	mov cx, 0x0002
	mov dh, 0
	mov bx, 0x7e00
	int 0x13
	jc dead
	xor ax, ax
	int 0x16			; the stage, a digit
	sub al, '0'
	cmp al, 17
	ja dead
	mov bl, al
	xor bh, bh
	shl bx, 1
	call [stages + bx]
dead:	cli
	hlt
	jmp dead

vseg:	push ax
	mov ax, 0xb800
	mov es, ax
	pop ax
	ret

; cx bytes at es:di: al, then al + ah, and so on.
fillb:	stosb
	add al, ah
	loop fillb
	ret

; Both fields of a graphics mode: 5 i + 1 in the even one, 11 i + 7 in
; the odd one.
fields:	call vseg
	xor di, di
	mov cx, 8000
	mov ax, 0x0501
	call fillb
	mov di, 0x2000
	mov cx, 8000
	mov ax, 0x0b07
	jmp fillb

; cx cells from es:0: ax, then ax + bx, and so on.
cells:	call vseg
	xor di, di
.next:	stosw
	add ax, bx
	loop .next
	ret

cursor:	mov ah, 2
	mov bh, 0
	int 0x10
	ret

; A key, polled for: the display is drawn many times before it arrives.
key:	mov ah, 1
	int 0x16
	jz key
	xor ax, ax
	int 0x16
	ret

	times 510 - ($ - $$) db 0
	dw 0xaa55

stages:	dw stage0, stage1, stage2, stage3, stage4, stage5, stage6, stage7, stage8, stage9
	dw stage10, stage11, stage12, stage13, stage14, stage15, stage16, stage17

; Mode 4 left for text and entered again with memory kept: the text mode's
; cells are what is kept, the rest reads as zeros. Then a disk sector read
; straight into display memory.
stage0:	mov ax, 4
	int 0x10
	call fields
	mov ax, 3
	int 0x10
	mov ax, 0x84			; keep memory: the cells stay
	int 0x10
	call vseg
	mov ax, 0x0201
	mov cx, 0x0001
	mov dx, 0
	mov bx, 0x0100
	int 0x13
	ret

; 80 columns: every character and attribute, the first eight characters
; the rows of the firmware font's letter A, and the cursor.
stage1:	mov ax, 3
	int 0x10
	mov cx, 2000
	mov ax, 0x4567
	mov bx, 0x0123
	call cells
	push ds
	mov ax, 0xf000
	mov ds, ax
	mov si, 0xfa6e + 8 * 'A'
	xor di, di
	mov cx, 8
.rom:	lodsb
	stosb
	inc di
	loop .rom
	pop ds
	mov dx, 0x050a
	jmp cursor

; 40 columns, the cursor in the last cell.
stage2:	mov ax, 1
	int 0x10
	mov cx, 1000
	mov ax, 0x89ab
	mov bx, 0x0357
	call cells
	mov dx, 0x1827
	jmp cursor

; Mode 4: the fields, pixels written and read, characters, both palettes.
stage3:	mov ax, 4
	int 0x10
	call vseg
	mov byte [es:0x1f41], 0xff	; past each field's last line
	mov byte [es:0x3f41], 0xff
	mov ax, 4			; the mode again: all of it cleared
	int 0x10
	call fields
	mov ax, 0x0c02
	mov cx, 17
	mov dx, 33
	int 0x10
	mov ax, 0x0c83			; exclusive or
	mov cx, 300
	mov dx, 199
	int 0x10
	mov ah, 0x0d
	mov cx, 17
	mov dx, 33
	int 0x10			; al: the 2 written above
	mov ah, 0x0c
	mov cx, 1
	mov dx, 1
	int 0x10
	mov al, [es:0x1f41]
	mov ah, 0x0c
	mov cx, 2
	mov dx, 2
	int 0x10
	mov al, [es:0x3f41]
	mov ah, 0x0c
	mov cx, 3
	mov dx, 2
	int 0x10
	mov dx, 0x0304
	call cursor
	mov ax, 0x0e41
	mov bl, 2
	int 0x10
	mov ax, 0x0e62
	mov bl, 3
	int 0x10
	mov dx, 0x0a25
	call cursor
	mov ax, 0x095a			; five of them where three fit
	mov bx, 0x0081
	mov cx, 5
	int 0x10
	mov ah, 0x0b
	mov bx, 0x0100			; green, red, brown
	int 0x10
	mov ah, 0x0b
	mov bx, 0x0011			; on blue, bright
	int 0x10
	ret

; Mode 6: the fields, pixels, and a character in the last cell, which
; scrolls the screen.
stage4:	mov ax, 6
	int 0x10
	call fields
	mov ax, 0x0c01
	mov cx, 639
	mov dx, 0
	int 0x10
	mov ax, 0x0c81
	mov cx, 5
	mov dx, 100
	int 0x10
	mov dx, 0x184f
	call cursor
	mov ax, 0x0e57
	mov bl, 1
	int 0x10
	mov ax, 0x0e78
	mov bl, 1
	int 0x10
	ret

; Mode 4: windows scrolled up and down, and the color register.
stage5:	mov ax, 4
	int 0x10
	call fields
	mov ax, 0x85			; mode 5, memory kept
	int 0x10
	mov ax, 0x0602
	mov bh, 0x55
	mov cx, 0x0203
	mov dx, 0x0a14
	int 0x10
	mov ax, 0x0701
	mov bh, 0xaa
	mov cx, 0x0c00
	mov dx, 0x1827
	int 0x10
	mov dx, 0x3d9
	mov al, 0x2a
	out dx, al
	ret

; Mode 4, drawn, then two bytes changed.
stage6:	mov ax, 4
	int 0x10
	mov ah, 0x0b			; the other palette and back
	mov bx, 0x0100
	int 0x10
	mov ah, 0x0b
	mov bx, 0x0101
	int 0x10
	call fields
	call key
	mov ah, 3			; asking draws nothing
	mov bh, 0
	int 0x10
	mov ah, 0x0f
	int 0x10
	mov dx, 0x0101
	call cursor
	mov byte [es:4000], 0x1b
	mov byte [es:0x2000 + 81], 0xe4
	ret

; 80 columns, drawn, then one cell changed, two rows scrolled by the
; firmware and the cursor moved.
stage7:	mov ax, 3
	int 0x10
	mov cx, 2000
	mov ax, 0x4567
	mov bx, 0x0123
	call cells
	mov dx, 0x050a
	call cursor
	call key
	mov word [es:2 * 1000], 0x1e58
	mov ax, 0x0601
	mov bh, 0x2f
	mov cx, 0x0e00
	mov dx, 0x0f4f
	int 0x10
	mov dx, 0x1405
	jmp cursor

; Mode 4 through the adapter's registers alone.
stage8:	mov dx, 0x3d8
	mov al, 0x0a
	out dx, al
	call fields
	mov dx, 0x3d9
	mov al, 0x30
	out dx, al
	ret

; A graphics mode, a key, and back to text; the text ends with the digit
; of the mode the firmware reported after the key.
stage9:	mov ax, 4
	int 0x10
	call fields
	call key
	mov ah, 0x0f
	int 0x10
	add al, '0'
	push ax
	mov ax, 3
	int 0x10
	mov si, text
.next:	lodsb
	test al, al
	jz .done
	mov ah, 0x0e
	mov bl, 7
	int 0x10
	jmp .next
.done:	pop ax
	mov ah, 0x0e
	mov bl, 7
	int 0x10
	ret

text:	db "TEXT", 0

; Mode 4, drawn, then writes that leave the adapter as it was: the color
; register, the firmware's background and palette service, the mode
; register and a mode set that keeps memory. Its key is ':'.
stage10:
	mov ax, 4
	int 0x10
	call fields
	call key
	mov dx, 0x3d9
	mov al, 0x30
	out dx, al
	mov ah, 0x0b			; background 0, bright: 30h again
	mov bx, 0x0010
	int 0x10
	mov dx, 0x3d8
	mov al, 0x0a
	out dx, al
	mov ax, 0x84
	int 0x10
	ret

; 80 columns, drawn, then the whole screen scrolled up one row by the
; firmware, a cell stored on the new row and the cursor moved there. Its
; key is '<'; a second key scrolls once more, and a third sets the mode
; again and stores a cell at the top.
stage12:
	mov ax, 3
	int 0x10
	mov cx, 2000
	mov ax, 0x4567
	mov bx, 0x0123
	call cells
	mov dx, 0x050a
	call cursor
	call key
	mov ax, 0x0601
	mov bh, 0x07
	xor cx, cx
	mov dx, 0x184f
	int 0x10
	mov word [es:2 * 1920], 0x1e5a
	mov dx, 0x1801
	call cursor
	call key
	mov ax, 0x0601			; and once more
	mov bh, 0x07
	xor cx, cx
	mov dx, 0x184f
	int 0x10
	mov word [es:2 * 1920], 0x1e59
	mov dx, 0x1802
	call cursor
	call key
	mov ax, 3			; a third key: a cleared screen
	int 0x10
	mov word [es:0], 0x1e41
	mov dx, 0x0001
	jmp cursor

; Mode 4, drawn, then mode 4 set again: its memory cleared, the panel
; black without a clear of its own. Its key is ';'.
stage11:
	mov ax, 4
	int 0x10
	call fields
	call key
	mov ax, 4
	int 0x10
	ret

; 80 columns with the top row blank and the others distinct, the cursor
; in the top left cell. Each key then scrolls the whole screen: its low
; five bits are the rows, and a lowercase key also writes a row of its own
; at the bottom, 80 cells of the key's number of scrolls so far as the
; attribute, which keeps every row distinct. The cursor moves to row 5,
; column 10 after each scroll. Its key is '='.
stage13:
	mov ax, 3
	int 0x10
	mov cx, 2000
	mov ax, 0x4567
	mov bx, 0x0123
	call cells
	xor di, di
	mov cx, 80
	mov ax, 0x0720
	rep stosw
	xor dx, dx
	call cursor
.next:	call key
	mov bp, ax
	and al, 0x1f
	mov ah, 6
	mov bh, 7
	xor cx, cx
	mov dx, 0x184f
	int 0x10
	test bp, 0x20
	jz .cur
	inc byte [count]
	mov ah, [count]
	xor al, al
	mov di, 2 * 1920
	mov cx, 80
.row:	stosw
	inc al
	loop .row
.cur:	mov dx, 0x050a
	call cursor
	jmp .next

count:	db 0

; Mode 4, drawn, then the palette select bit alone cleared through the
; firmware: nothing else about the adapter's registers changes. Its key
; is '>'.
stage14:
	mov ax, 4
	int 0x10
	call fields
	call key
	mov ah, 0x0b
	mov bx, 0x0100
	int 0x10
	ret

; Mode 4, drawn, then the color burst bit alone set through the mode
; register. Its key is '?'.
stage15:
	mov ax, 4
	int 0x10
	call fields
	call key
	mov dx, 0x3d8
	mov al, 0x0e
	out dx, al
	ret

; 80 columns, the whole screen scrolled one row and drawn, then mode 4: the
; scroll start the text mode left must not rotate the graphics lines. Its
; keys are '@', and two more.
stage16:
	mov ax, 3
	int 0x10
	mov cx, 2000
	mov ax, 0x4567
	mov bx, 0x0123
	call cells
	call key
	mov ax, 0x0601
	mov bh, 0x07
	xor cx, cx
	mov dx, 0x184f
	int 0x10
	call key
	mov ax, 4
	int 0x10
	jmp fields

; Mode 4, drawn, then mode 6 and its fields: two graphics modes in a row.
; Its key is 'A'.
stage17:
	mov ax, 4
	int 0x10
	call fields
	call key
	mov ax, 6
	int 0x10
	jmp fields

	times 1536 - ($ - $$) db 0
