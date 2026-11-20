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

	bits 16
	cpu 8086
	org 0x7c00

SECTORS	equ 4			; this program, boot sector included
BUF1	equ 0x9000
BUF2	equ 0x9200

start:	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	sti
	cld
	mov [drive], dl
	mov ax, 0x0200 + SECTORS - 1
	mov bx, 0x7e00
	mov cx, 0x0002
	mov dh, 0
	int 0x13
	jc .bad
	jmp main
.bad:	mov si, m_load
	call puts
; After the tests: each r typed reads the boot sector of the drive again
; and prints the six-letter label at its end with a count, so a changed
; disk shows.
halt:	xor ah, ah
	int 0x16
	cmp al, 'r'
	jne halt
	mov ax, 0x0201
	mov bx, BUF2
	mov cx, 0x0001
	mov dh, 0
	mov dl, [drive]
	int 0x13
	jc halt
	mov si, BUF2 + 0x1f8
	mov cx, 6
.label:	lodsb
	mov ah, 0x0e
	int 0x10
	loop .label
	inc byte [reads]
	mov al, [reads]
	mov ah, 0x0e
	int 0x10
	jmp halt

puts:	lodsb
	or al, al
	jz .ret
	mov ah, 0x0e
	int 0x10
	jmp puts
.ret:	ret

; AX as four hexadecimal digits.
hex4:	push cx
	mov cx, 4
.digit:	push cx
	mov cl, 4
	rol ax, cl
	pop cx
	push ax
	and al, 15
	add al, '0'
	cmp al, '9'
	jbe .put
	add al, 7
.put:	mov ah, 0x0e
	int 0x10
	pop ax
	loop .digit
	pop cx
	ret

drive:	db 0
reads:	db '0'
m_load:	db "LOAD BAD", 13, 10, 0

	times 0x1f8 - ($ - $$) db 0
	db "DISK-A"
	dw 0xaa55

main:	; Thirty lines scroll the screen before the results.
	mov cx, 30
.line:	mov si, m_line
	call puts
	mov ax, cx
	call hex4
	mov si, m_crlf
	call puts
	loop .line

	; The processor, by the stack pointer PUSH SP stores and by the
	; count a shift accepts.
	mov si, m_286
	push sp
	pop ax
	cmp ax, sp
	je .cpu
	mov si, m_186
	mov ax, 1
	mov cl, 32
	shl ax, cl
	jnz .cpu
	mov si, m_8086
.cpu:	call puts

	; Memory: every kilobyte above 64 KB is first written with a
	; zero, which leaves it modified and empty, then with two words,
	; one of them across a kilobyte boundary.
	mov si, m_mem
	call puts
	int 0x12
	push ax
	call hex4
	pop ax
	mov cl, 6
	shl ax, cl
	mov [top], ax
	mov bx, 0x1000
.zero:	mov es, bx
	mov word [es:0x200], 0
	add bx, 0x40
	cmp bx, [top]
	jb .zero
	mov bx, 0x1000
.fill:	mov es, bx
	mov ax, bx
	mov [es:2], ax
	not ax
	mov [es:0x3ff], ax
	add bx, 0x40
	cmp bx, [top]
	jb .fill
	mov si, m_ok
	mov bx, 0x1000
.check:	mov es, bx
	mov ax, bx
	cmp [es:2], ax
	jne .mbad
	not ax
	mov dx, [top]
	sub dx, 0x40
	cmp bx, dx
	jae .next		; the last word runs into absent memory
	cmp [es:0x3ff], ax
	jne .mbad
.next:	add bx, 0x40
	cmp bx, [top]
	jb .check
	jmp .mdone
.mbad:	mov si, m_bad
.mdone:	call puts

	; IDIV with the quotient -128: the 8086 raises interrupt 0 and
	; returns past the instruction, the later processors store 80h.
	mov word [0], div0
	mov [2], cs
	mov ax, 0xff80
	mov bl, 1
	idiv bl
	mov si, m_trap
	cmp byte [trapped], 0
	jne .idiv
	mov si, m_q80
	cmp al, 0x80
	je .idiv
	mov si, m_ibad
.idiv:	call puts

	; A word at offset FFFFh has its second byte at offset 0 of the
	; same segment, not in the next one.
	mov ax, 0x2000
	mov es, ax
	mov byte [es:0], 0x12
	mov ax, 0x3000
	mov es, ax
	mov byte [es:0], 0x99
	mov ax, 0x2000
	mov es, ax
	mov byte [es:0xffff], 0x34
	mov si, m_wbad
	cmp word [es:0xffff], 0x1234
	jne .wrap
	mov word [es:0xffff], 0xabcd
	cmp byte [es:0], 0xab
	jne .wrap
	mov ax, 0x3000
	mov es, ax
	cmp byte [es:0], 0x99
	jne .wrap
	mov si, m_wrap
.wrap:	call puts
	xor ax, ax
	mov es, ax

	; Disk: write a sector, read it back, read past the end.
	mov di, BUF1
	mov cx, 512
	mov al, 3
.pat:	stosb
	add al, 7
	loop .pat
	mov ax, 0x0301
	mov bx, BUF1
	mov cx, 0x0101
	mov dh, 0
	mov dl, [drive]
	int 0x13
	jc .dbad
	mov ax, 0x0201
	mov bx, BUF2
	mov cx, 0x0101
	mov dh, 0
	mov dl, [drive]
	int 0x13
	jc .dbad
	mov si, BUF1
	mov di, BUF2
	mov cx, 256
	repe cmpsw
	jne .dbad
	mov ax, 0x0201
	mov bx, BUF2
	mov cx, 0xff01		; cylinder 255: past every format
	mov dh, 0
	mov dl, [drive]
	int 0x13
	jnc .dbad
	mov si, m_disk
	call puts
	mov ah, 8
	mov dl, [drive]
	int 0x13
	mov ax, cx
	call hex4
	mov ax, dx
	call hex4
	; The drive type and the sectors a track of its parameter table.
	mov ah, bl
	mov al, [es:di + 4]
	call hex4
	xor ax, ax
	mov es, ax
	mov si, m_crlf
	jmp .ddone
.dbad:	mov si, m_dbad
.ddone:	call puts

	; Timer: the tick count moves while the processor is halted, and
	; a handler on interrupt 1Ch runs with interrupts disabled.
	xor ah, ah
	int 0x1a
	mov bx, dx
.tick:	hlt
	xor ah, ah
	int 0x1a
	cmp dx, bx
	je .tick
	mov si, m_tick
	call puts
	; The interrupt also arrives between instructions of a loop.
	mov bx, [0x46c]
.spin:	cmp bx, [0x46c]
	je .spin
	mov si, m_spin
	call puts
	; The keyboard service returns with interrupts enabled: a caller
	; that polls it with interrupts off still sees the ticks move.
	cli
	mov bx, [0x46c]
.kbd:	mov ah, 1
	int 0x16
	cmp bx, [0x46c]
	je .kbd
	sti
	mov si, m_kbd
	call puts
	; A masked timer line delivers nothing; unmasked, the tick it held
	; back arrives.
	mov al, 1
	out 0x21, al
	mov bx, [0x46c]
	xor cx, cx
.mask:	loop .mask
	mov si, m_mkbad
	cmp bx, [0x46c]
	jne .masked
	xor al, al
	out 0x21, al
.open:	cmp bx, [0x46c]
	je .open
	mov si, m_mask
.masked: xor al, al
	out 0x21, al
	call puts
	cli
	mov word [0x1c * 4], hook
	mov [0x1c * 4 + 2], cs
	sti
.hook:	hlt
	cmp word [count], 2
	jb .hook
	mov si, m_hook
	test word [hflags], 0x0200	; entered with interrupts off
	jz .hooked
	mov si, m_hbad
.hooked: call puts

	; Display: cells written straight into display memory and through
	; the firmware, and read back.
	mov ax, 0xb800
	mov es, ax
	mov di, 6 * 160		; six lines scroll after this
	mov si, m_direct
	mov ah, 0x1e
.cell:	lodsb
	or al, al
	jz .cells
	stosw
	jmp .cell
.cells:	xor ax, ax
	mov es, ax
	mov ah, 3
	xor bh, bh
	int 0x10
	push dx
	mov ah, 2
	mov dx, 0x0705
	int 0x10
	mov ax, 0x095a
	mov bx, 0x004f
	mov cx, 3
	int 0x10
	mov ah, 8
	int 0x10
	pop dx
	push ax
	mov ah, 2
	int 0x10
	pop ax
	mov si, m_vid
	cmp ax, 0x4f5a
	je .vid
	mov si, m_vbad
.vid:	call puts

	; A request for the monochrome mode selects mode 3. The mode is
	; set without clearing, and the mode and cursor are put back.
	mov ah, 3
	xor bh, bh
	int 0x10
	push dx
	mov ah, 0x0f
	int 0x10
	push ax
	mov ax, 0x0087
	int 0x10
	mov ah, 0x0f
	int 0x10
	mov cl, al
	pop ax
	xor ah, ah
	or al, 0x80
	int 0x10
	pop dx
	mov ah, 2
	xor bh, bh
	int 0x10
	mov si, m_mode
	cmp cl, 3
	je .mode
	mov si, m_mobad
.mode:	call puts

	; Keyboard: three keys, each echoed with its key code, and the
	; timer ticks that passed while waiting for them. A handler on
	; vector 9 counts the scan bytes at port 60h on the way.
	cli
	mov ax, [9 * 4]
	mov [old9], ax
	mov ax, [9 * 4 + 2]
	mov [old9 + 2], ax
	mov word [9 * 4], key9
	mov [9 * 4 + 2], cs
	sti
	mov ax, [0x46c]
	mov [t0], ax
	mov cx, 3
.key:	xor ah, ah
	int 0x16
	push ax
	mov ah, 0x0e
	int 0x10
	mov al, '='
	int 0x10
	pop ax
	call hex4
	mov al, ' '
	mov ah, 0x0e
	int 0x10
	loop .key
	mov si, m_wait
	call puts
	mov ax, [0x46c]
	sub ax, [t0]
	call hex4
	; a, Shift with B, 1: eight scan bytes, the last the release of 1;
	; then no shift flag stays set and nothing stays in service.
.scans:	hlt
	cmp byte [nscan], 8
	jb .scans
	mov si, m_irq
	call puts
	mov ah, [nscan]
	mov al, [lscan]
	call hex4
	mov ah, 2
	int 0x16
	mov bl, al
	mov al, 0x0b
	out 0x20, al
	in al, 0x20
	or al, bl
	jz .irqok
	mov si, m_ibad2
	call puts
.irqok:	mov si, m_done
	call puts
	jmp halt

key9:	push ax
	in al, 0x60
	mov [cs:lscan], al
	inc byte [cs:nscan]
	pop ax
	jmp far [cs:old9]

div0:	mov byte [cs:trapped], 1
	iret

; Counts the ticks and keeps the flags it was entered with.
hook:	push ax
	pushf
	pop ax
	mov [cs:hflags], ax
	pop ax
	inc word [cs:count]
	iret

top:	dw 0
old9:	dw 0, 0
nscan:	db 0
lscan:	db 0
t0:	dw 0
trapped: db 0
count:	dw 0
hflags:	dw 0
m_line:	db "LINE ", 0
m_crlf:	db 13, 10, 0
m_8086:	db "CPU 8086", 13, 10, 0
m_186:	db "CPU 186", 13, 10, 0
m_286:	db "CPU 286", 13, 10, 0
m_mem:	db "MEM ", 0
m_ok:	db " OK", 13, 10, 0
m_bad:	db " BAD", 13, 10, 0
m_trap:	db "IDIV TRAP", 13, 10, 0
m_q80:	db "IDIV 80", 13, 10, 0
m_ibad:	db "IDIV BAD", 13, 10, 0
m_wrap:	db "WRAP OK", 13, 10, 0
m_wbad:	db "WRAP BAD", 13, 10, 0
m_disk:	db "DISK OK ", 0
m_dbad:	db "DISK BAD", 13, 10, 0
m_tick:	db "TICK OK", 13, 10, 0
m_spin:	db "SPIN OK", 13, 10, 0
m_kbd:	db "KBD OK", 13, 10, 0
m_mask:	db "MASK OK", 13, 10, 0
m_mkbad: db "MASK BAD", 13, 10, 0
m_irq:	db 13, 10, "IRQ ", 0
m_ibad2: db " BAD", 0
m_hook:	db "HOOK OK", 13, 10, 0
m_hbad:	db "HOOK BAD", 13, 10, 0
m_direct: db "DIRECT", 0
m_vid:	db "VID OK", 13, 10, 0
m_vbad:	db "VID BAD", 13, 10, 0
m_wait:	db 13, 10, "WAIT ", 0
m_mode:	db "MODE OK", 13, 10, 0
m_mobad: db "MODE BAD", 13, 10, 0
m_done:	db 13, 10, "DONE", 13, 10, 0

	times SECTORS * 512 - ($ - $$) db 0
