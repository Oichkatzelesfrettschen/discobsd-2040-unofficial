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
; The guest of the expanded memory test (tests/check.py): a boot program
; that calls interrupt 67h, the firmware's EMS service, and records one
; token a line, "name:HEX". The lines go to the display and, at the end,
; to sectors 1 and 2 of head 1 of its own disk, where check.py reads them
; after a run on the panel display too. Assemble with
;
;	nasm -f bin -o ems.bin ems.asm
;
; ems.hexdump is the assembled program, 32 bytes a line. With the digit 1
; typed at the start the program sets graphics mode 4, which lends page
; frames to the display, before it reads back the pages it wrote.

	bits 16
	cpu 8086
	org 0x7c00

OUTBUF	equ 0x0600			; the lines, written to disk at the end

start:	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00
	mov word [optr], OUTBUF
	mov ax, 0x0206			; the six sectors after this one
	mov cx, 0x0002
	xor dh, dh
	mov bx, 0x7e00
	int 0x13
	jc dead
	jmp main
dead:	cli
	hlt
	jmp dead

; One character: on the display and into the buffer.
putc:	push ax
	push bx
	push di
	mov di, [optr]
	mov [di], al
	inc word [optr]
	mov ah, 0x0e
	mov bx, 7
	int 0x10
	pop di
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

; "label:XX" for the label at si and al; "label:XXXX" for ax.
say2:	push ax
	call puts
	pop ax
	call hex2
	jmp crlf
say4:	push ax
	call puts
	pop ax
	call hex4
	jmp crlf

; Status of the call just made: ah.
st:	push ax
	mov al, ah
	call say2
	pop ax
	ret

optr:	dw 0

	times 510 - ($ - $$) db 0
	dw 0xaa55

; es = segment, fill 16 KB with (seed + offset) & 255.
fill:	push ax
	push cx
	push di
	xor di, di
	mov cx, 0x4000
.next:	mov al, dl
	add ax, di
	stosb
	loop .next
	pop di
	pop cx
	pop ax
	ret

; es = segment: ax = a checksum of the 16 KB, rotate and add by words.
sum:	push cx
	push si
	push ds
	push es
	pop ds
	xor si, si
	xor ax, ax
	mov cx, 0x2000
.next:	rol ax, 1
	add ax, [si]
	add si, 2
	loop .next
	pop ds
	pop si
	pop cx
	ret

; Map handle [h] page bx at physical page al.
; AL: the high byte of physical page 1's word in the current map (FFh
; when unmapped). Leaves ES = 0 and pbuf holding the map.
e400hi:	xor ax, ax
	mov es, ax
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67
	mov al, [pbuf + 5]
	mov dx, 0xe000
	mov es, dx
	ret

map:	push dx
	mov dx, [h]
	mov ah, 0x44
	int 0x67
	pop dx
	ret

h:	dw 0
b1:	dw 0
area1:	times 2 + 2 * 18 db 0
area2:	times 2 + 2 * 18 db 0
mlist:	dw 2, 0xe000, 0, 0xc000		; logical, segment
mreg:	dd 20				; 57h: length
	db 0				; from conventional memory
	dw 0, msrc, 0
	db 1				; to handle [h], page 1, offset 3FF6h
mdst_h:	dw 0, 0x3ff6, 1
xreg:	dd 4
	db 0
	dw 0, xsrc, 0
	db 1
xdst_h:	dw 0, 0x3ff6, 1
msrc:	db "EXPANDED MEMORY OK.."
xsrc:	db "SWAP"
mbuf:	times 20 db 0

main:	mov ah, 0x40
	int 0x67
	mov si, s_st
	call st
	test ah, ah
	jz .have
	jmp done
.have:	mov ah, 0x41
	int 0x67
	mov ax, bx
	mov si, s_frame
	call say4
	mov ah, 0x42
	int 0x67
	mov ax, bx
	mov si, s_free
	call say4
	mov ax, dx
	mov si, s_total
	call say4
	mov ah, 0x46
	int 0x67
	mov si, s_ver
	call say2
	mov ah, 0x58			; the mappable pages
	mov al, 1
	int 0x67
	mov ax, cx
	mov si, s_phys
	call say4
	mov ah, 0x43			; three pages
	mov bx, 3
	int 0x67
	mov [h], dx
	mov [mdst_h], dx
	mov [xdst_h], dx
	mov si, s_alloc
	call st
	mov ax, dx
	mov si, s_handle
	call say4
	mov ah, 0x42
	int 0x67
	mov ax, bx
	mov si, s_free
	call say4
	mov ah, 0x43			; more than there are, than are free, none
	mov bx, 0x0100
	int 0x67
	mov si, s_e87
	call st
	mov ah, 0x43
	mov bx, 14
	int 0x67
	mov si, s_e88
	call st
	mov ah, 0x43
	xor bx, bx
	int 0x67
	mov si, s_e89
	call st
	; three pages filled through physical page 0
	mov ax, 0xe000
	mov es, ax
	xor bx, bx
.fill:	mov al, 0
	call map
	mov dl, bl
	shl dl, 1
	add dl, 0x11
	call fill
	inc bx
	cmp bx, 3
	jb .fill
	; page 2 read whole, so that every page of it is mapped when a
	; graphics mode borrows frames
	mov ax, 0xe000
	mov es, ax
	call sum
	; graphics mode 4 and back, when 1 was typed: a loan of frames
	mov ah, 0
	int 0x16
	cmp al, '1'
	jne .nogfx
	mov ax, 4
	int 0x10
	mov ax, 3
	int 0x10
.nogfx:	; page 2, still mapped at physical page 0, read before any remap
	mov ax, 0xe000
	mov es, ax
	call sum
	mov si, s_sumx
	call say4
	; each page read back, page 1 through two physical pages
	xor bx, bx
.sum:	mov al, 0
	call map
	call sum
	mov si, s_sum
	call say4
	inc bx
	cmp bx, 3
	jb .sum
	mov bx, 1
	mov al, 0
	call map
	mov bx, 1
	mov al, 3
	call map
	mov ax, 0xe000
	mov es, ax
	mov byte [es:5], 0xab		; page 1 at physical 0 and 3
	mov ax, 0xec00
	mov es, ax
	mov al, [es:5]
	mov si, s_alias
	call say2
	mov bx, 2			; page 2 refilled: the pager evicts
	mov al, 2
	call map
	mov ax, 0xe800
	mov es, ax
	mov dl, 0x77
	call fill
	mov ax, 0xec00			; and page 1 again through physical 3
	mov es, ax
	mov al, [es:5]
	mov si, s_alias2
	call say2
	; a disk sector read into physical page 2 lands in page 2
	mov bx, 2
	mov al, 2
	call map
	mov ax, 0xe800
	mov es, ax
	mov ax, 0x0201
	mov cx, 0x0001
	xor dx, dx
	mov bx, 0
	int 0x13
	mov bx, 0
	mov al, 2
	call map
	mov bx, 2
	mov al, 2
	call map
	mov ax, 0xe800
	mov es, ax
	mov ax, [es:0x1fe]
	mov si, s_disk
	call say4
	; save and restore the page frame
	mov bx, 0
	mov al, 0
	call map
	mov ah, 0x47
	mov dx, [h]
	int 0x67
	mov si, s_save
	call st
	mov ah, 0x47
	int 0x67
	mov si, s_e8d
	call st
	mov bx, 2
	mov al, 0
	call map
	mov ah, 0x48
	mov dx, [h]
	int 0x67
	mov ax, 0xe000
	mov es, ax
	mov al, [es:0]
	mov si, s_restore
	call say2
	; several pages by segment
	mov ax, 0x5001
	mov cx, 2
	mov dx, [h]
	mov si, mlist
	int 0x67
	mov si, s_multi
	call st
	mov ax, 0xc000
	mov es, ax
	mov al, [es:0]
	mov si, s_atc000
	call say2
	; an array longer than the mappable pages: 8Bh and nothing mapped.
	; pbuf is still zero: 19 valid entries, logical 0 at physical 0.
	; E000:0 keeps the byte it held (31h), not logical 0's 11h.
	mov ax, 0x5000
	mov cx, 19
	mov dx, [h]
	mov si, pbuf
	int 0x67
	mov si, s_e50l
	call st
	mov ax, 0xe000
	mov es, ax
	mov al, [es:0]
	mov si, s_e000l
	call say2
	; move from conventional memory across the end of page 1
	push ds
	pop es
	mov ax, 0x5700
	mov si, mreg
	int 0x67
	mov si, s_move
	call st
	mov ax, 0x5701			; and exchange 4 bytes back
	mov si, xreg
	int 0x67
	mov si, s_xchg
	call st
	mov al, [xsrc]
	mov si, s_xgot
	call say2
	mov ax, 0x5700			; the moved bytes, back out
	mov si, mreg
	push word [mreg + 5]
	push word [mreg + 7]
	push word [mreg + 0x0c]
	mov byte [mreg + 4], 1
	mov ax, [h]
	mov [mreg + 5], ax
	mov word [mreg + 7], 0x3ff6
	mov word [mreg + 9], 1
	mov byte [mreg + 0x0b], 0
	mov word [mreg + 0x0c], 0
	mov word [mreg + 0x0e], mbuf
	mov word [mreg + 0x10], 0
	mov ax, 0x5700
	int 0x67
	mov si, mbuf + 10
	mov ax, [si]
	mov si, s_moved
	call say4
	mov bx, 2			; and where they landed: page 2 from 0
	mov al, 0
	call map
	mov ax, 0xe000
	mov es, ax
	mov ax, [es:0]
	mov si, s_landed
	call say4
	; the alternate map set: maps made while a set is current are its own
	push ds
	pop es
	mov ax, 0x4e00
	mov di, area1
	int 0x67
	mov ax, 0x4e00
	mov di, area2
	int 0x67
	mov ax, 0x5b01
	xor bl, bl
	mov di, area1
	int 0x67
	mov si, s_set1
	call st
	mov bx, 1
	mov al, 0
	call map
	push ds
	pop es
	mov ax, 0x5b01
	xor bl, bl
	mov di, area2
	int 0x67
	mov ax, [area1 + 2]		; physical page 0 in area 1
	push ax
	mov ax, [area2 + 2]
	sub ax, [area1 + 2]
	mov si, s_alt
	call say4
	pop ax
	mov bx, 0			; 5B00h stores the current map
	mov al, 0
	call map
	mov ax, 0x5b00
	int 0x67
	mov ax, [area2 + 2]
	sub ax, [area1 + 2]
	mov si, s_get
	call say4
	; handle 0 keeps no pages
	mov ah, 0x51
	xor dx, dx
	xor bx, bx
	int 0x67
	mov si, s_h0
	call st
	mov ah, 0x51
	inc bx
	int 0x67
	mov si, s_e88b
	call st
	; errors: handle, logical page, physical page
	mov ah, 0x44
	mov al, 0
	xor bx, bx
	mov dx, 9
	int 0x67
	mov si, s_e83
	call st
	mov bx, 7
	mov al, 0
	call map
	mov si, s_e8a
	call st
	mov bx, 0
	mov al, 40
	call map
	mov si, s_e8b
	call st
	mov ah, 0x55
	int 0x67
	mov si, s_e84
	call st
	; 4Dh into a buffer that wraps at 1 MB: the second entry lands at 0:0
	push word [0]
	push word [2]
	mov word [0], 0xffff
	mov ax, 0xffff
	mov es, ax
	mov di, 0x000c
	mov ah, 0x4d
	int 0x67
	mov ax, [0]
	pop word [2]
	pop word [0]
	mov si, s_wrap
	call say4
	; 57h from handle page 0 to E000:0001 while that page shows at E000h
	mov ax, [h]
	mov [oreg + 5], ax
	mov bx, 0
	mov al, 0
	call map
	mov ax, 0x5700
	mov si, oreg
	int 0x67
	mov si, s_e94
	call st
	mov bx, 1			; and while another page shows there
	mov al, 0
	call map
	mov ax, 0x5700
	mov si, oreg
	int 0x67
	mov si, s_e94no
	call st
	mov ax, 0x5b06			; no DMA register sets
	xor bl, bl
	mov dl, 1
	int 0x67
	mov si, s_e9e
	call st
	; 5Dh: the access key
	mov ax, 0x5d00
	int 0x67
	mov [okey], bx
	mov [okey + 2], cx
	mov si, s_os
	call st
	mov ax, 0x5d01
	mov bx, [okey]
	mov cx, [okey + 2]
	xor cx, 1
	int 0x67
	mov si, s_osbad
	call st
	mov ax, 0x5d01
	mov cx, [okey + 2]
	int 0x67
	mov si, s_osoff
	call st
	mov ax, 0x5b00
	int 0x67
	mov si, s_e5b
	call st
	push ds
	pop es
	mov di, mbuf
	mov ax, 0x5900
	int 0x67
	mov si, s_e59
	call st
	mov ax, 0x5d02
	mov bx, [okey]
	mov cx, [okey + 2]
	int 0x67
	mov si, s_osret
	call st
	mov ax, 0x5d02			; with no key out
	int 0x67
	mov si, s_osnone
	call st
	mov ax, 0x5b00
	int 0x67
	mov si, s_oson
	call st
	; 4Fh: a count above the 18 physical pages, and a bad second entry
	mov bx, 1
	mov al, 0
	call map
	push ds
	pop es
	mov ax, 0x4f00
	mov si, p4f0
	mov di, pbuf
	int 0x67
	mov si, s_e4f0
	call st
	mov ax, 0x4f01
	mov si, p4f1
	int 0x67
	mov si, s_e4f1
	call st
	mov ax, 0x4f01
	mov si, p4fbad
	int 0x67
	mov si, s_e4fb
	call st
	mov ax, 0xe000			; physical page 0 still shows page 1
	mov es, ax
	mov al, [es:0]
	mov si, s_kept
	call say2
	; unmapped physical page 3 reads FFh
	mov bx, 0xffff
	mov al, 3
	call map
	mov ax, 0xec00
	mov es, ax
	mov al, [es:0]
	mov si, s_unmap
	call say2
	mov ah, 0x47			; a saved context keeps the handle
	mov dx, [h]
	int 0x67
	mov ah, 0x45
	int 0x67
	mov si, s_e86
	call st
	; A save area inside a page the call remaps is read once. E000h
	; shows logical 0 holding the area; its entry 0 maps logical 1 at
	; E000h, whose word at offset 4 is FFFFh, and its entry 1 keeps
	; logical 2 at E400h, which a second read would replace by FFFFh.
	xor ax, ax
	mov es, ax
	mov al, 1
	mov bx, 2
	call map
	xor al, al
	mov bx, 1
	call map
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67			; pbuf + 2: logical 1's word
	mov ax, 0xe000
	mov es, ax
	mov word [es:4], 0xffff
	xor al, al
	xor bx, bx
	call map
	xor di, di
	mov ax, 0x4e00
	int 0x67			; the area, at E000:0000
	mov ax, [pbuf + 2]
	mov [es:2], ax
	push ds
	push es
	pop ds
	xor si, si
	mov ax, 0x4e01
	int 0x67
	pop ds
	mov si, s_toc
	call st
	xor ax, ax
	mov es, ax
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67
	mov al, [pbuf + 5]
	mov si, s_tocp
	call say2
	; 4E02h with one buffer for both: the new map is read before the
	; old one is stored over it, so E400h ends unmapped.
	mov word [pbuf + 4], 0xffff
	mov si, pbuf
	mov di, pbuf
	mov ax, 0x4e02
	int 0x67
	mov si, s_ovl
	call st
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67
	mov al, [pbuf + 5]
	mov si, s_ovlp
	call say2
	; pbuf now holds the map: E000h logical 1 (its word, B1, at pbuf + 2),
	; E400h unmapped. 4F00h with one buffer for both: both segments are
	; read before the first pair is stored over the second.
	mov ax, [pbuf + 2]
	mov [b1], ax
	mov word [pbuf], 2
	mov word [pbuf + 2], 0xe000
	mov word [pbuf + 4], 0xe400
	mov si, pbuf
	mov di, pbuf
	mov ax, 0x4f00
	int 0x67
	mov si, s_p4o
	call st
	; 4F01h from a partial map inside a page its entry 0 remaps: entry 1
	; maps logical 1 at E400h; logical 1's words at that offset say unmap.
	mov ax, 0xe000
	mov es, ax
	mov word [es:6], 0xe400		; E000h shows logical 1 here
	mov word [es:8], 0xffff
	xor al, al
	xor bx, bx
	call map			; logical 0 at E000h, for the map
	mov ax, [b1]
	mov word [es:0], 2
	mov word [es:2], 0xe000
	mov [es:4], ax
	mov word [es:6], 0xe400
	mov [es:8], ax
	push ds
	push es
	pop ds
	xor si, si
	mov ax, 0x4f01
	int 0x67
	pop ds
	mov si, s_p4t
	call st
	call e400hi
	mov si, s_p4tp
	call say2
	; 50h from a list inside a page its entry 0 remaps: entry 1 maps
	; logical 1 at physical page 1; logical 1 there says unmap.
	mov word [es:0x14], 0xffff	; E000h shows logical 1 again
	mov word [es:0x16], 1
	xor al, al
	xor bx, bx
	call map
	mov word [es:0x10], 1
	mov word [es:0x12], 0
	mov word [es:0x14], 1
	mov word [es:0x16], 1
	push ds
	push es
	pop ds
	mov si, 0x10
	mov cx, 2
	mov dx, [cs:h]
	mov ax, 0x5000
	int 0x67
	pop ds
	mov si, s_m50
	call st
	call e400hi
	mov si, s_m50p
	call say2
	; 5B01h with the new set where the old set lies: the new set is
	; read before the old one is stored over it.
	xor ax, ax
	mov es, ax
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67			; E400h shows logical 1
	mov word [pbuf + 4], 0xffff
	mov di, pbuf
	mov ax, 0x5b01
	xor bl, bl
	int 0x67			; set 0 is pbuf: E400h unmapped
	mov ax, [b1]
	mov [pbuf + 4], ax
	mov di, pbuf
	mov ax, 0x5b01
	xor bl, bl
	int 0x67
	mov si, s_alo
	call st
	call e400hi
	mov si, s_alop
	call say2
	xor di, di
	mov ax, 0x5b01
	xor bl, bl
	int 0x67			; back to no set 0
	; 5B00h returns the pointer 5B01h was given, unnormalized.
	xor ax, ax
	mov es, ax
	mov di, pbuf
	mov ax, 0x4e00
	int 0x67
	mov di, pbuf
	mov ax, 0x5b01
	xor bl, bl
	int 0x67
	mov ax, 0x5b00
	int 0x67
	mov ax, es
	mov si, s_altes
	call say4
	xor ax, ax
	mov es, ax
	xor di, di
	mov ax, 0x5b01
	xor bl, bl
	int 0x67
	; 57h from the firmware ROM: FFFF:0005 holds the date "01/01/90".
	mov si, rreg
	mov ax, 0x5700
	int 0x67
	mov si, s_rom
	call st
	mov al, [mbuf + 2]
	mov si, s_romb
	call say2
	; 57h past 1 MB, and from a page beyond the handle's three.
	mov si, wreg
	mov ax, 0x5700
	int 0x67
	mov si, s_ea2
	call st
	mov ax, [h]
	mov [lreg + 5], ax
	mov si, lreg
	mov ax, 0x5700
	int 0x67
	mov si, s_e8am
	call st
	; Logical 0 at E000h and E400h: a move of 512 bytes from E000:0000
	; to E400:0001 runs from the end, so byte 101h ends as byte 100h was
	; (0); a forward copy leaves byte FFh there. An exchange answers 97h.
	xor bx, bx
	xor al, al
	call map
	xor bx, bx
	mov al, 1
	call map
	mov ax, 0xe000
	mov es, ax
	xor dl, dl
	call fill
	mov si, areg
	mov ax, 0x5700
	int 0x67
	mov si, s_amovs
	call st
	mov al, [es:0x101]
	mov si, s_amov
	call say2
	mov si, areg
	mov ax, 0x5701
	int 0x67
	mov si, s_axch
	call st
	; Crossed windows: E000h and EC00h show logical 0, E400h and E800h
	; logical 1. A 32 KB move from E000:0000 to E800:0000 writes each page
	; that the other half still has to read, in both orders: 97h.
	mov bx, 1
	mov al, 1
	call map
	mov bx, 1
	mov al, 2
	call map
	xor bx, bx
	mov al, 3
	call map
	mov si, creg
	mov ax, 0x5700
	int 0x67
	mov si, s_acyc
	call st
	; 5401h with a name of nulls: A1h while a handle has no name, A0h
	; once handle 0 and [h] have names.
	mov si, nm0
	mov ax, 0x5401
	int 0x67
	mov si, s_nul1
	call st
	mov si, nm0 + 8
	xor dx, dx
	mov ax, 0x5301
	int 0x67
	mov si, nm0 + 16
	mov dx, [h]
	mov ax, 0x5301
	int 0x67
	mov si, nm0
	mov ax, 0x5401
	int 0x67
	mov si, s_nul0
	call st
	mov dx, [h]
	mov ah, 0x48
	int 0x67
	mov ah, 0x45
	int 0x67
	mov si, s_free2
	call st
	mov ah, 0x42
	int 0x67
	mov ax, bx
	mov si, s_free
	call say4
done:	mov si, s_done
	call puts
	call crlf
	xor ax, ax			; the lines to head 1, sectors 1 and 2
	mov es, ax
	mov bx, OUTBUF
	mov ax, 0x0302
	mov cx, 0x0001
	mov dx, 0x0100
	int 0x13
	jmp dead

s_st:	db "st:", 0
s_frame: db "frame:", 0
s_free:	db "free:", 0
s_total: db "total:", 0
s_ver:	db "ver:", 0
s_phys:	db "phys:", 0
s_alloc: db "alloc:", 0
s_handle: db "handle:", 0
s_e87:	db "e87:", 0
s_e88:	db "e88:", 0
s_e89:	db "e89:", 0
s_sum:	db "sum:", 0
s_sumx:	db "sumx:", 0
s_alias: db "alias:", 0
s_alias2: db "alias2:", 0
s_disk:	db "disk:", 0
s_save:	db "save:", 0
s_e8d:	db "e8d:", 0
s_restore: db "restore:", 0
s_multi: db "multi:", 0
s_atc000: db "c000:", 0
s_e50l:	db "e50l:", 0
s_e000l: db "e000l:", 0
s_move:	db "move:", 0
s_xchg:	db "xchg:", 0
s_xgot:	db "xgot:", 0
s_moved: db "moved:", 0
s_landed: db "landed:", 0
s_get:	db "get:", 0
s_set1:	db "set1:", 0
s_alt:	db "alt:", 0
s_h0:	db "h0:", 0
s_e88b:	db "e88h0:", 0
s_e83:	db "e83:", 0
s_e8a:	db "e8a:", 0
s_e8b:	db "e8b:", 0
s_e84:	db "e84:", 0
s_unmap: db "unmap:", 0
s_e86:	db "e86:", 0
s_toc:	db "toc:", 0
s_tocp:	db "tocp:", 0
s_ovl:	db "ovl:", 0
s_ovlp:	db "ovlp:", 0
s_p4o:	db "p4o:", 0
s_p4t:	db "p4t:", 0
s_p4tp:	db "p4tp:", 0
s_m50:	db "m50:", 0
s_m50p:	db "m50p:", 0
s_alo:	db "alo:", 0
s_alop:	db "alop:", 0
s_free2: db "dealloc:", 0
s_wrap:	db "wrap:", 0
s_e94:	db "e94:", 0
s_e94no: db "e94no:", 0
s_e9e:	db "e9e:", 0
s_os:	db "os:", 0
s_osbad: db "osbad:", 0
s_osoff: db "osoff:", 0
s_e5b:	db "e5b:", 0
s_e59:	db "e59:", 0
s_osret: db "osret:", 0
s_oson:	db "oson:", 0
s_osnone: db "osnone:", 0
s_e4f0:	db "e4f0:", 0
s_e4f1:	db "e4f1:", 0
s_e4fb:	db "e4fb:", 0
s_kept:	db "kept:", 0
s_altes: db "altes:", 0
s_rom:	db "rom:", 0
s_romb:	db "romb:", 0
s_ea2:	db "ea2:", 0
s_e8am:	db "e8am:", 0
s_amovs: db "amovs:", 0
s_amov:	db "amov:", 0
s_axch:	db "axch:", 0
s_acyc:	db "acyc:", 0
s_nul1:	db "nul1:", 0
s_nul0:	db "nul0:", 0
s_done:	db "done", 0
p4f0:	dw 19			; 4F00h: 19 segments
	times 19 dw 0xe000
p4f1:	dw 19			; 4F01h: 19 entries, each a valid unmap
	times 19 dw 0xe000, 0xffff
p4fbad:	dw 2, 0xe000, 0xffff, 0x1000, 0xffff
pbuf:	times 2 + 4 * 19 db 0
okey:	dd 0
oreg:	dd 16			; 57h: from handle page 0 ...
	db 1
	dw 0, 0, 0
	db 0				; ... to E000:0001
	dw 0, 1, 0xe000
rreg:	dd 8			; 57h: the ROM date at FFFF:0005 to mbuf
	db 0
	dw 0, 5, 0xffff
	db 0
	dw 0, mbuf, 0
wreg:	dd 16			; 57h: FFFF:0010, past 1 MB
	db 0
	dw 0, 0x10, 0xffff
	db 0
	dw 0, mbuf, 0
lreg:	dd 0			; 57h: handle [h], page 3
	db 1
	dw 0, 0, 3
	db 0
	dw 0, mbuf, 0
areg:	dd 512			; 57h: E000:0000 to E400:0001
	db 0
	dw 0, 0, 0xe000
	db 0
	dw 0, 1, 0xe400
creg:	dd 32768		; 57h: E000:0000 to E800:0000, crossed
	db 0
	dw 0, 0, 0xe000
	db 0
	dw 0, 0, 0xe800
nm0:	times 8 db 0
	db "OS-ZERO!"
	db "TESTPAGE"

	times 3584 - ($ - $$) db 0
