# PicoCalc console blanker: design and cost

Decision D216 asks for a kernel blanker: after an idle period the console puts the panel to sleep through the controller's power commands, and a key wakes it. This note records the design, the measured cost of a prototype and what an implementation still needs. The implementation lands after the GizmoBSD rename (D109); the prototype is kept on `archive/proto-console-blanker-2026-11-22`.

## What sleep saves

The ST7365P datasheet (page 50, section 7.3) gives the controller's analog supply current as 12 mA typical and 16 mA maximum in normal mode, and 0.025 mA typical and 0.050 mA maximum in sleep (all pixels black, 60 Hz, default registers). Sleep in (SLPIN, 10h, page 163) stops the DC/DC converter, the oscillator and panel scanning, while "MCU interface and memory are still working and the memory keeps its contents". So the console can keep drawing into panel memory during sleep, and the screen is current when the panel wakes.

Display off (DISPOFF, 28h) is the other low-power mode `LCDG_POWER` offers. Page 50 omits its current, and it leaves the oscillator and scanning running, so it is not used here.

The LED backlight is a separate load, driven by the keyboard controller through its register 05h. Its current has not been measured. A blanker that also dims the backlight would need to send that register write without sleeping, since the keyboard poll that runs the blanker cannot sleep; `picokbd_reg()` sleeps until the reply.

## Design

- **Idle count:** the keyboard poll (`kbd_poll`, `picokbd.c`) calls `lcdcons_blank_poll()` on every exchange. A key, or an open `/dev/lcdgfx`, records `time.tv_sec` and asks for the panel awake. `LCD_BLANK_SECS` (300 by default) without a key asks for sleep. A change pends the render interrupt.
- **Change in the renderer:** `lcdrender()` makes the change with the drawing flag held, so no draw shares the bus with SLPIN or SLPOUT. Pages 163 and 165 set two gaps. Every SLPIN or SLPOUT needs 5 ms before the next command, which the renderer waits after sending it; it already draws in 8 ms slices. An SLPIN also needs 120 ms after the last SLPOUT, and inside that window the renderer schedules a callout for the remainder instead of waiting. A wake therefore costs 5 ms. The prototype applies the 120 ms gap to both directions, which the datasheet does not ask for. `lcd_gfx_power()` and `st7365p_abort()` also wait 120 ms after SLPIN, which is conservative.
- **Power state:** `st7365p_sleep()` sends the one command and keeps `lcd_power_mode` and `lcd_power_pending` current, so `st7365p_abort()` wakes a sleeping panel before a fault report, as it does for `LCDG_POWER`.
- **Graphics programs:** `/dev/lcdgfx` owns the panel's power while it is open, and its last close powers the panel on through `lcd_gfx_power()`. Its open does not, so the open path must wake a sleeping panel itself (SLPOUT, then 5 ms) before its first drawing command. The prototype only marks the blanker awake in `lcdcons_gfx()` and leaves the panel asleep until the close.

## Measured cost

PICOCALC_SD at `e32078513`, `arm-none-eabi-gcc` 16.2.0, built warning-free with `-Wall -Wextra -Werror`:

| Build | text | data | bss | flash image | flash headroom |
|---|---:|---:|---:|---:|---:|
| main | 130,280 | 144 | 58,944 | 130,184 | 33,400 |
| with `LCD_BLANK` | 130,584 | 144 | 58,952 | 130,696 | 32,888 |
| difference | +304 | 0 | +8 | +512 | -512 |

`arm-none-eabi-nm -S` places 108 bytes in `lcdcons_blank_poll` and 56 in `st7365p_sleep`. The rest is the change code inlined into `lcdrender` and the calls from `kbd_poll` and `lcdcons_gfx`. The 8 bss bytes hold the two state bytes, the time of the last key and the time of the last change.

## Open before an implementation lands

- **Key handling:** the key that wakes the panel also reaches the tty as input. The alternative, swallowing the first key, costs a flag and is the owner's choice.
- **Idle start:** the prototype leaves the time of the last key at zero, while `time.tv_sec` starts from the root file system's timestamp (`sys/kern/init_main.c`), so it blanks at the first empty keyboard exchange after boot. The implementation counts idle time from a monotonic tick count started with the console, which `settimeofday()` cannot move.
- **Wake at graphics open:** the prototype does not wake the panel at a graphics open (Design, above). The implementation sends SLPOUT on that path.
- **Host gate:** the gate must drive `lcdcons_blank_poll()` and the renderer against a model panel and require each of the following: the 120 ms gap before SLPIN, the 5 ms wait after either command, the idle count from boot, the wake at a graphics open, sleep held off while `/dev/lcdgfx` is open, and the fault-path wake. Mutants that drop each of these must fail it.
- **Board check:** a board run must show the console text intact after a wake, and the supply current with the panel asleep and awake.
- **Build quirk found while measuring:** after a kernel's `Config` is edited, the first build compiles with the old `PARAM`. The `reconfig ioconf.c` rule rewrites the Makefile that the running `bmake` has already read. Adding `options LCD_BLANK` produced a baseline-sized kernel on the first build and the 130,584-byte kernel on the second.
