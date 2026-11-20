# General-purpose pins: /dev/porta and /dev/portb

Build and host evidence only: the register path has run on no board and no
emulator.

`sys/arch/rp2040/dev/gpio.c` gives the superuser the pins a board leaves
free, through the request numbers of the RetroBSD PIC32 GPIO driver
(`machine/gpio.h` carries them; the PIC32 header is kept under
`legacy/non-arm/mips-pic32/sys/sys/gpio.h`). Character major 12; minor 0
is `/dev/porta`, GP0-GP15, and minor 1 is `/dev/portb`, GP16-GP29. A
request's low byte names the port, as in the PIC32 driver, so one open file
reaches both.

## Which pins

| Board | Reachable | Held by the kernel or the board |
| --- | --- | --- |
| Raspberry Pi Pico | GP2-GP22, GP26-GP28 | GP0, GP1 (UART0); GP23 (regulator mode), GP24 (VBUS sense), GP25 (LED), GP29 (VSYS divider) |
| PicoCalc | GP28 (expansion header J703) | every other pin: `PICOCALC.md`, "Pin map" |

The device is part of a kernel configured with the PicoCalc console and
the SD driver, whose character majors 10 and 11 fix its own at 12; that is
the union kernel every board runs. The PICO and PICO_UART configurations
keep majors 0-9 and have no such device. The union kernel reads the board
from its descriptor (`board_id()`), so one image applies the right row. A request that names a held pin has no
effect on it, and `GPIO_POLL` reports held pins as zero.

## Requests

| Request | Effect on the pins the mask names |
| --- | --- |
| `GPIO_CONFIN` | input: SIO function, output disabled, pad at its reset setting (input enabled, pull-down) |
| `GPIO_CONFOUT` | output: SIO function, output enabled, level as last stored |
| `GPIO_DECONF` | released: output disabled, no function |
| `GPIO_STORE` | every configured pin of the port takes its bit of the mask |
| `GPIO_SET`, `GPIO_CLEAR`, `GPIO_INVERT` | configured pins go high, go low, change |
| `GPIO_POLL` | returns the input levels of the port's reachable pins |
| `GPIO_PWM` | PWM: the mask's pins take the PWM function at the fixed carrier; the argument is the mask in its low 16 bits and the duty, 0-255, in its high 16 (see PWM) |
| `GPIO_CONFOD` | refused with `EINVAL`: the RP2040 pad has no open-drain mode |

Requests combine as in the PIC32 driver (`GPIO_CONFOUT | GPIO_SET`).
Output requests act only on pins this device configured, and configuration
outlives the close. The PIC32 driver's text interface (`echo ..1. >
/dev/porta`) and its `/dev/confa` nodes are absent: `read()` and
`write()` return `ENODEV`.

## Analog input

`machdep.adc.N` (mib `{ CTL_MACHDEP, CPU_ADC, N }`, `sysctl machdep.adc.N`) returns the raw 12-bit
count of ADC input N, 0 to 4, one conversion per read, for any user. Input
0-2 are GP26-GP28, input 3 is GP29 (the Pico's VSYS divider) and input 4 is
the die sensor; the reference is the 3.3 V ADC_AVDD, so a count is
3300 / 4096 mV. Each read of inputs 0-3 first sets its pad to analog (no
function, input buffer, output and both pulls off), then converts. The node
refuses by the table below and the kernel `DEV_ADC` option gates it (a
kernel without it answers `EOPNOTSUPP` as the two older nodes do).

| Input | Pin | Pico | PicoCalc |
| --- | --- | --- | --- |
| 0, 1 | GP26, GP27 | read | `EPERM`: the audio amplifier pins |
| 2 | GP28 | read | read; `EBUSY` while the GPIO device has configured the pin |
| 3 | GP29 | read (VSYS / 3) | read |
| 4 | die sensor | read | read |
| 5 and up | none | `EINVAL` | `EINVAL` |

The `EBUSY` rule applies to inputs 0-2 on every board (on a Pico, input 0
is GP26 and input 1 is GP27). A pin released with `GPIO_DECONF` reads again.
Reading an input leaves its pad analog, so a program that wants the pin as
a digital input configures it again.

## Ownership table

`gpio_board_claims()` in `<machine/gpio.h>` is the one table of what a board
leaves to programs and what the kernel keeps back; `GPIO_PWM`,
`machdep.adc.N`, `/dev/audio` and the digital requests read it and keep no
pin list of their own.

| Board | Free pins | PWM slices the kernel always runs | Slices run while `/dev/audio` is open |
| --- | --- | --- | --- |
| Pico | GP2-GP22, GP26-GP28 | none | none |
| PicoCalc | GP28 | 5 (the bell and `/dev/audio`'s carrier) | 6 (`/dev/audio`'s sample clock) |
| any other board | none | none | none |

GP28 is slice 6 channel A (datasheet 4.5.2, the PWM function of GPIO 28), and
slice 6 carries `/dev/audio`'s sample clock (`picopcm.c`: its TOP, divider and
wrap interrupt). One slice has one counter, so a program's PWM on GP28 and
the sample clock cannot run together. The driver does not take the slice
from either side: `GPIO_PWM` on GP28 answers `EBUSY` while `/dev/audio` is open,
and `/dev/audio` answers `EBUSY` to an open while GP28 is a PWM pin. The input
side has no such conflict: ADC input 2 reads GP28's pad and uses no slice.

The interface names a pin, an input and a duty. A program never passes a
register address or value, a PWM slice, a channel or the carrier's divider;
the kernel derives them from the pin.

## PWM

`GPIO_PWM` (command bit 26, a request the PIC32 driver lacks) exists in a kernel
built with `options DEV_GPIO_PWM`, which PICOCALC_SD lists and PICOCALC and the
lean kernels leave out (PICOCALC text +152 bytes with the option out of it
and +512 with it in); elsewhere it answers `EOPNOTSUPP`. It runs the pins of
its mask from their PWM slices (datasheet 4.5): `ioctl(fd, GPIO_PORTA | GPIO_PWM,
1 << 8 | 128 << 16)` puts GP8 at 128 of 255. `GPIO_PWM` stands alone; it is
refused beside another command. Every slice counts `clk_sys / 255` and wraps
after 255 counts, so the carrier is 1,922 Hz at a 125 MHz `clk_sys`, a pin is
high for `duty` counts of each period, duty 0 is a steady low and duty 255
holds the pin high. The two channels of a slice (GP8 and GP9 share slice 4)
keep separate duties on one carrier. The pad takes the reset setting of the
digital pins (input buffer on, pull-down).

Unlike the digital requests, which ignore a pin a board holds, `GPIO_PWM`
fails: `EINVAL` for a pin the board holds (the table above), an empty mask
or a duty past 255, and `EBUSY` for a slice the kernel uses.

| Case | Result |
| --- | --- |
| PicoCalc, GP28 (slice 6 A) | runs; `EBUSY` while `/dev/audio` is open |
| PicoCalc, GP8, GP9 (slice 4) | `EINVAL`: the pins are the keyboard controller's UART lines |
| PicoCalc, slice 5 (GP10, GP11, GP26, GP27) | `EINVAL`: the board holds those pins; the bell and `/dev/audio`'s carrier use the slice |
| PicoCalc, any other pin | `EINVAL` |
| Pico, a free pin (slices 1-7, GP26 and GP27 on slice 5 included) | runs: no Pico kernel driver uses a slice |
| Pico, GP0, GP1, GP23-GP25, GP29 | `EINVAL` |

A PWM pin is also an owned pin. `GPIO_DECONF` stops it (no function, as for any
owned pin), a digital `GPIO_CONFIN` or `GPIO_CONFOUT` takes it over, and the
last close of any port node stops every PWM pin, so a program that exits
releases its pins; d_close runs once per node, so a second process holding
the other node has its PWM stopped too. A stopped slice keeps counting;
it routes no pin. `/dev/audio` answers `EBUSY` to an open while a PWM pin
is on slice 6, and a PWM request on slice 6 answers `EBUSY` while it is open;
neither side takes the slice from the other.

## Evidence

| Claim | Class | State |
| --- | --- | --- |
| Pin masks, port mapping and request validation | host | `check-gpio-requests`, six mutants rejected |
| PICOCALC compiles and links the driver; PICOCALC_LEAN and PICOCALC_LEAN_DIAG keep the slot as `NOCDEV`; PICO and PICO_UART keep majors 0-9 | build | observed; `check-kernel-metadata` pins the rows |
| A configured output drives its pin; an input reads its level; a held pin stays untouched | board | not run; `tests/rp2040/gpioprobe` decides it |
| `GPIO_PWM` decoding, board pin refusals, slice ownership against the audio drivers, duty range, carrier and compare word | host | `check-gpio-requests`, twenty-one mutants rejected |
| `GPIO_PWM` drives GP28 of a PicoCalc and a free pin of a Pico at the stated duty and frequency; `/dev/audio` and slice 6 refuse each other; the last close stops the pins | board | not run; an oscilloscope on GP28 of a PicoCalc (duty sweep, 128 of 255 at 1.92 kHz, `EBUSY` with `/dev/audio` open) and on GP2 of a bare Pico would decide it |
| Kernel cost against origin/main (GCC 16.2.0 `-Os`, text/data/bss): PICO 98,402/128/42,704 to 98,522/128/42,704; PICOCALC 127,444/144/53,104 to 127,596/144/53,104; PICOCALC_SD 128,940/144/53,176 to 129,452/144/53,176 | build | observed; `DEV_GPIO_PWM` is in PICOCALC_SD only, which carries the 360 bytes of PWM code PICOCALC leaves out |
| `machdep.adc.N` input refusals: board-owned pins, pins the GPIO device holds, inputs past the sensor | host | `check-adc-conversion`, four mutants rejected |
| Input 2 on GP28 follows the voltage at the PicoCalc's expansion header | board | not run; a bench supply at 0 V, 1.65 V and 3.3 V on GP28 should read near 0, 2048 and 4095 |

`tests/rp2040/gpioprobe` drives GP2 of a bare Pico high and low through
`/dev/porta` and reads it back, requests the UART0 pins and checks that
the poll leaves them out, and expects `EINVAL` for open drain, `ENXIO` for
port 2 and `ENODEV` for `read`. Each step prints `gpio.<step>=pass|fail`;
the exit status counts the failures. GP2 must be unconnected while it
runs. Stage it with a `file /usr/bin/gpioprobe` line in
`distrib/rp2040/mi.rp2040` for a test image and revert that line before
committing.
