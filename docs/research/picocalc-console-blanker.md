# PicoCalc console blanker: design and cost

Decision D216 asks for a kernel blanker: after an idle period the console puts the panel to sleep through the controller's power commands, and a key wakes it. This note records the design, the measured cost of a prototype and what an implementation still needs. The implementation lands after the GizmoBSD rename (D109); the prototype is kept on `archive/proto-console-blanker-2026-11-22`.

## What sleep saves

The ST7365P datasheet (page 50, section 7.3) gives the controller's analog supply current as 12 mA typical and 16 mA maximum in normal mode, and 0.025 mA typical and 0.050 mA maximum in sleep (all pixels black, 60 Hz, default registers). Sleep in (SLPIN, 10h, page 163) stops the DC/DC converter, the oscillator and panel scanning, while "MCU interface and memory are still working and the memory keeps its contents". So the console can keep drawing into panel memory during sleep, and the screen is current when the panel wakes.

Display off (DISPOFF, 28h) is the other low-power mode `LCDG_POWER` offers. Page 50 omits its current, and it leaves the oscillator and scanning running, so it is not used here.

The LED backlight is a separate load, driven by the keyboard controller through its register 05h. Its current has not been measured. A blanker that also dims the backlight would need to send that register write without sleeping, since the keyboard poll that runs the blanker cannot sleep; `picokbd_reg()` sleeps until the reply.

## Design

- **Idle count:** the keyboard poll (`kbd_poll`, `picokbd.c`) calls `lcdcons_blank_poll()` on every exchange. A key, or an open `/dev/lcdgfx`, records `time.tv_sec` and asks for the panel awake. `LCD_BLANK_SECS` (300 by default) without a key asks for sleep. A change pends the render interrupt.
- **Change in the renderer:** `lcdrender()` makes the change with the drawing flag held, so no draw shares the bus with SLPIN or SLPOUT. It needs at least 120 ms since the previous change, which pages 163 and 165 require between SLPIN and SLPOUT. Inside that window it schedules a callout for the remainder instead of waiting. After the command it waits the 5 ms the controller needs before its next command; the renderer already draws in 8 ms slices.
- **Power state:** `st7365p_sleep()` sends the one command and keeps `lcd_power_mode` and `lcd_power_pending` current, so `st7365p_abort()` wakes a sleeping panel before a fault report, as it does for `LCDG_POWER`.
- **Graphics programs:** `/dev/lcdgfx` owns the panel's power while it is open. Its open and last close power the panel on through `lcd_gfx_power()`, and `lcdcons_gfx()` marks the blanker awake at both.

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
- **Gap at graphics open:** `lcd_gfx_power()` waits 120 ms after SLPOUT, but not between a blanker SLPIN and the SLPOUT that a graphics open sends. The implementation passes the time of the last change to it.
- **Host gate:** the gate must drive `lcdcons_blank_poll()` and the renderer against a model panel and require each of the following: the 120 ms gap, the 5 ms wait, sleep held off while `/dev/lcdgfx` is open, and the fault-path wake. Mutants that drop each of these must fail it.
- **Board check:** a board run must show the console text intact after a wake, and the supply current with the panel asleep and awake.
- **Build quirk found while measuring:** after a kernel's `Config` is edited, the first build compiles with the old `PARAM`. The `reconfig ioconf.c` rule rewrites the Makefile that the running `bmake` has already read. Adding `options LCD_BLANK` produced a baseline-sized kernel on the first build and the 130,584-byte kernel on the second.
