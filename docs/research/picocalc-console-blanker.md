# PicoCalc console blanker: design and cost

Decision D216 asks for a kernel blanker: after an idle period the console puts the panel to sleep through the controller's power commands, and a key wakes it. This note records what sleep saves, the design an implementation follows, the measured cost of a prototype, and the defects review found in that prototype. The implementation lands after the GizmoBSD rename (D109). The prototype is kept on `archive/proto-console-blanker-2026-11-22` for its measurement, not as a base.

## What sleep saves

The ST7365P datasheet (page 50, section 7.3) gives the controller's analog supply current as 12 mA typical and 16 mA maximum in normal mode, and 0.025 mA typical and 0.050 mA maximum in the row it labels "Stand by Mode" (all pixels black, 60 Hz, default registers). The datasheet does not name the command that enters that mode, and it defines a separate deep standby (`DSTB` in B7h, page 228). Reading "Stand by Mode" as sleep in (SLPIN, 10h) is therefore an inference. The board current measurement listed below decides it.

Page 163 says sleep in stops the DC/DC converter, the oscillator and panel scanning, while "MCU interface and memory are still working and the memory keeps its contents". So the console can keep drawing into panel memory during sleep, and the screen is current when the panel wakes.

`LCDG_POWER` offers three modes besides on: idle (IDMON, 39h, eight colors with the display lit), display off (DISPOFF, 28h) and sleep. Page 50 omits a current for display off, and display off leaves the oscillator and scanning running, so the blanker uses sleep.

The LED backlight is a separate load, driven by the keyboard controller through its register 05h. Its current has not been measured. A blanker that also dims the backlight would need to send that register write without sleeping, since the keyboard poll that runs the blanker cannot sleep; `picokbd_reg()` sleeps until the reply.

## Design

- **Idle count:** each completed keyboard exchange (`kbd_poll`, `picokbd.c`) reports whether it read a key. A key, or an open `/dev/lcdgfx`, restarts the idle count, which runs on a monotonic tick count started with the console. `settimeofday()` cannot move that count, and it does not start from zero at boot. `LCD_BLANK_SECS` (300 by default) without a key asks for sleep, and a key asks for wake. A change of the request pends the render interrupt.
- **Gaps:** pages 163 and 165 require 5 ms after SLPIN or SLPOUT before any other command, and 120 ms from SLPOUT to the next SLPIN. A wake therefore costs 5 ms. Every path that sends SLPIN must see the time of the last SLPOUT, including `lcd_gfx_power()` for `LCDG_POWER`. Today every SLPOUT in `st7365p.c` waits 120 ms after itself, so `lcd_gfx_power()` never needed that time. A 5 ms wake changes that.
- **Change in the renderer:** `lcdrender()` makes the change with the drawing flag held, so no draw shares the bus with SLPIN or SLPOUT. SysTick preempts the renderer, and a key callout can change the request while a command is going out. So the renderer records the state it actually sent (a snapshot of the request), and pends itself again when the request differs afterward. Inside the 120 ms window before an SLPIN it arms one dedicated callout for the remainder. A flag keeps that callout single, since `timeout()` accepts duplicates and the callout table holds 18 entries. It is distinct from `render_resume`, so cancelling it cannot cancel a pending output slice.
- **Fault path:** `lcdabandon()` calls `st7365p_abort()` only while the drawing flag is held or `/dev/lcdgfx` is open. A panel the blanker put to sleep meets neither condition, so the guard must also cover a sleeping or transitioning panel. `lcd_power_pending` is set before either command is sent and cleared only after the 5 ms gap that follows SLPOUT. A fault anywhere in a transition then finds it set.
- **Graphics programs:** `/dev/lcdgfx` owns the panel's power while it is open, and its last close powers the panel on through `lcd_gfx_power()`. Its open does not. So the open path wakes a sleeping panel through the same helper the renderer uses before `lcdgfx_open()` sends its first command (`lcd_gfx_colmod()`). That helper sends SLPOUT, waits 5 ms, then clears `lcd_power_pending` and records the time of the SLPOUT.

## Measured cost

PICOCALC_SD at `e32078513`, `arm-none-eabi-gcc` 16.2.0, built warning-free with `-Wall -Wextra -Werror`:

| Build | text | data | bss | flash image | flash headroom |
|---|---:|---:|---:|---:|---:|
| main | 130,280 | 144 | 58,944 | 130,184 | 33,400 |
| with `LCD_BLANK` | 130,584 | 144 | 58,952 | 130,696 | 32,888 |
| difference | +304 | 0 | +8 | +512 | -512 |

`arm-none-eabi-nm -S` places 108 bytes in `lcdcons_blank_poll` and 56 in `st7365p_sleep`. The rest is the change code inlined into `lcdrender`, the call from `kbd_poll` and the state reset in `lcdcons_gfx`. The four new statics take 10 bytes (two state bytes, the time of the last key and the time of the last change). The linked bss grows by 8 because 4 bytes of existing 8-byte alignment padding absorb part of them. The design above adds a callout flag and the wake helper, so an implementation costs somewhat more than the prototype. Expect tens of bytes, to be measured.

## Prototype defects found in review

Each departs from the design above, and each needs a case in the host gate:

1. It counts idle time from `time.tv_sec` with the last-key time at zero, so it blanks at the first keyboard poll after boot. Stepping the clock back with `settimeofday()` also blanks at once, because the kernel's `time_t` is unsigned.
2. It calls the idle check on every keyboard poll phase, not on completed exchanges.
3. It applies the 120 ms gap to both directions, which delays a wake by up to 120 ms.
4. It records the request as it stands after the command, which can mark the panel awake while it sleeps and lose the wake.
5. It arms a new `render_resume` callout on every renderer entry inside the 120 ms window, which can fill the callout table.
6. It sets `lcd_power_pending` after the command and clears it at once after SLPOUT. It also leaves `lcdabandon()`'s guard unchanged, so a fault while the panel sleeps draws its report on a sleeping panel.
7. It marks the blanker awake at a graphics open without waking the panel, which stays asleep until the close.

## Open before an implementation lands

- **Key handling:** the key that wakes the panel also reaches the tty as input. The alternative, swallowing the first key, costs a flag and is the owner's choice.
- **Host gate:** the gate drives the idle check and the renderer against a model panel. It requires the 5 ms and 120 ms gaps in their directions, the idle count from boot and across a clock step, the wake at a graphics open before its first command, and a request changed while a command goes out. It also requires one deferral callout at a time, and a fault report on a lit panel for faults injected before and after each command and while asleep. A mutant for each of the seven defects above must fail it.
- **Board check:** a board run must show the console text intact after a wake, and the supply current with the panel asleep and awake.
- **Build quirk found while measuring:** after a kernel's `Config` is edited, the first build compiles with the old `PARAM`. The `reconfig ioconf.c` rule rewrites the Makefile that the running `bmake` has already read. Adding `options LCD_BLANK` produced a baseline-sized kernel on the first build and the 130,584-byte kernel on the second.
