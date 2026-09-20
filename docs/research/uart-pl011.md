# RP2040 UART0 console: PL011 driver and Renode verification

`sys/arch/rp2040/dev/uart.c` drives UART0 as the console through the ARM
PL011 registers `uart.h` declares, replacing the PIC32 USART register code
that survived the port inside `#if 0 // XXX` blocks. No PIC32 identifier
remains in the file: `grep -E 'PIC32|USTA|UMODE|BRG|reg->'` over `uart.c`
returns nothing.

## Registers the driver uses

The driver reaches the PL011 at `UART0_BASE` 0x40034000, RP2040 datasheet
section 4.2.8.

- `uartinit` releases IO_BANK0, PADS_BANK0, and UART0 from reset through the
  RESETS block, then sets GPIO0 and GPIO1 to function 2 (UART0 TX and RX per
  the datasheet GPIO function table) by writing `PADS_BANK0_GPIO` input
  enable and `IO_BANK0_CTRL` funcsel 2.
- The baud divisors come from clk_peri, which `machdep.c`'s
  `SystemClock_Config` runs from clk_sys at BUS_KHZ (125000). The divisor is
  `clk_peri / (16 * baud)`; the integer part goes to `UARTIBRD` and the
  fraction times 64 to `UARTFBRD`. `uart_set_baud` computes
  `div = 4 * clk_peri / baud` and splits it as `ibrd = div >> 6`,
  `fbrd = div & 0x3f`, which for 125 MHz and 115200 baud gives IBRD 67 and
  FBRD 52, the datasheet's own worked example.
- `UARTLCR_H` is written `WLEN_8 | FEN` (8N1 with the FIFOs enabled); the
  write latches the divisors.
- `UARTCR` is written `UARTEN | TXE | RXE`.
- `UARTIMSC` unmasks `UART_INT_RX | UART_INT_RT` (RXIM bit 4 and RTIM bit 6).
  The receive-timeout matters: PL011 raises RX only once the FIFO passes its
  trigger level, so a lone keystroke reaches the handler through the timeout
  alone.
- UART0's NVIC line, interrupt 20 (datasheet section 2.3.2), is given
  `IPL_TTY` priority and enabled through `arm_intr_enable_irq`, matching how
  `usbinit` enables `USBCTRL_IRQ`.

Transmit waits for `UARTFR` TXFF to clear, then writes `UARTDR`; `uartputc`
keeps this usable from a masked context under `spltty`, matching `usbputc`.
Receive in `uartintr` drains the FIFO while `UARTFR` RXFE is clear, feeding
each byte to `ttyinput`, then acknowledges `UART_INT_RX | UART_INT_RT`
through `UARTICR`. The ICR write is the one functional fix beyond deleting
the PIC32 blocks: draining the FIFO drops the RX level interrupt on its own,
but PL011 holds the receive-timeout latched until an explicit clear, so
without the write the first timeout would wedge the receive path.

## Renode verification

`tools/renode/boot.resc` boots the `PICO_UART` kernel from the real RP2040
boot ROM against the matgla/Renode_RP2040 peripheral models, pinned at commit
205a5e4b, whose UART (PL011) model the project's README marks fully
supported. `git log 205a5e4b..origin/HEAD` on that repository is empty, so
this is the latest published model.

### Boot output over UART0

The kernel reaches the swap line over UART0, captured from the socket
`boot.resc` opens:

(The banner line `no MMU and no MPU` in this capture is what the kernel printed then; datasheet 2.4.1 and 2.4.6 give the Cortex-M0+ an eight-region MPU, and the kernel now prints `no MMU` and an `mpu:` line stating what it programmed; sys/arch/rp2040/doc/MPU.md.)

```
DiscoBSD 2.7 (PICO_UART) #1 877: Fri Sep 11 22:27:32 PDT 2026
     eirikr@x570-5600X3D:/sys/arch/rp2040/compile/PICO_UART
cpu: RP0000 rev 0, manufacturer 0x000
cpu: Cortex-M0+, ARMv6-M, no MMU and no MPU
cpu: 125 MHz core, 125 MHz peripheral
fl0: 989 kbytes on QSPI flash, 1536 kbytes raw
fl0a: partition type b7, sector 2, size 988 kbytes
fl1: 384 kbytes raw QSPI flash for swap
phys mem  = 264 kbytes
user mem  = 96 kbytes
root dev  = (0,1)
swap dev  = (0,8)
root size = 988 kbytes
swap size = 384 kbytes
```

Every one of those bytes leaves the PL011 through the driver's transmit
path, proving TX, the baud divisors, the GPIO0 function mux, and the whole
console print path end to end. Renode's log confirms the pin mux from the
other side: `clocks: Update of GPIO mapping for pin: 0 function: UART0_TX`
and `pin: 1 function: UART0_RX`.

### Receive interrupt delivery

The receive path is exercised by feeding bytes into the UART0 socket during
boot -- after `machdep.c` calls `uartinit(0)`, which enables the console
UART's receive interrupt, and before the boot ROM stall below. A GDB
breakpoint on `uartintr` halts on the interrupt:

```
Breakpoint 1, uartintr (dev=dev@entry=1536) at .../dev/uart.c:295
UARTFR=0  UARTIMSC=0x50  UARTMIS=0x10
#0  uartintr (dev=dev@entry=1536) at .../dev/uart.c:295
#1  0x10010d22 in UART0_IRQ_Handler () at .../dev/uart.c:175
#2  <signal handler called>
#3  0x0000001c in ?? ()
```

The backtrace runs from the NVIC vector through `UART0_IRQ_Handler` into
`uartintr`, so IRQ 20 delivery and the handler wiring are live. `UARTIMSC`
reads 0x50, exactly `UART_INT_RX | UART_INT_RT`, confirming the unmask.
`UARTFR` reads 0 (RXFE clear), so a byte sits in the receive FIFO, and
`UARTMIS` 0x10 names RXMIS as the source. The handler runs to completion and
re-enters on the next byte, so the drain and the `UARTICR` acknowledge do
not wedge it. `ttyinput` past its early return is not reached during this
window because the console tty is not `TS_ISOPEN` until `init` opens
`/dev/console`, which happens after root mount -- the stage the boot ROM
stall below blocks.

### login is blocked upstream of the UART, in the boot ROM

Past `swap size` the kernel does not reach `login:`. GDB finds the CPU
spinning in the real boot ROM image (`bootroms/rp2040/b2.elf` at address 0),
inside `flash_put_get`:

```
pc=0x178e r12=0x4001801c r6=0x1
=> 0x178e:	ldr	r6, [r4, #36]	@ 0x24
   0x1790:	cmp	r2, #0
```

`$r12` holds 0x4001801c, `IO_QSPI_BASE + 0x1c`, the CTRL register of QSPI pin
3; the ROM polls its pad-override readback and never sees the bits it waits
on. This is the QSPI-pad model gap `emulation.md` already documents, and the
register value matches that report byte for byte. The stall reaches this ROM
routine through the kernel's flash program or erase path -- Dhara's journal
resume during root mount, which runs under `splhigh` -- so once the CPU
enters the loop nothing else in the kernel runs. The routine lives in a
vendored C# peripheral (`RP2040QspiPads`) at the pinned commit, upstream of
the UART entirely; no UART change moves it.

### Harness correction this verification required

`boot.resc` loaded `flash.bin` at 0x10030000, the filesystem offset from an
earlier layout. `flash.h` now sets `FLASH_FS_OFFSET` to 128 KB, so the XIP
address the kernel reads is 0x10020000 (`unix.map`'s FSFLASH), and `flashimg`
writes the image for that offset. At the stale address the kernel found no
filesystem and panicked `No root filesystem found! no fs on dev (0,1)`.
Correcting the load address to 0x10020000 is what advanced the boot from that
panic to the `fl0a` partition line and the real boot ROM stall. The two
stale 0x10030000 references in `BOOT-MAP.md` are corrected in the same
change so the map matches the harness.

## Does the board still need a real serial adapter?

Yes, to confirm the interactive path. Renode verifies transmit end to end and
receive-interrupt delivery, priority, and unmask, but its boot ROM QSPI-pad
model stalls the flash write path before `getty` runs, so a typed `root`
login and a command cannot be shown in emulation today. On silicon the same
ROM path reaches `login:` (BOOT-MAP.md section 7), so the login/getty layer
over this driver is the one piece a board with a serial adapter would
confirm that this emulator cannot.
