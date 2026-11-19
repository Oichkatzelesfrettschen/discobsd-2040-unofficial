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
| PicoCalc | GP8, GP9, GP28 (the expansion header) | every other pin: `PICOCALC.md`, "Pin map" |

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

## Evidence

| Claim | Class | State |
| --- | --- | --- |
| Pin masks, port mapping and request validation | host | `check-gpio-requests`, six mutants rejected |
| PICOCALC compiles and links the driver; PICOCALC_LEAN and PICOCALC_LEAN_DIAG keep the slot as `NOCDEV`; PICO and PICO_UART keep majors 0-9 | build | observed; `check-kernel-metadata` pins the rows |
| A configured output drives its pin; an input reads its level; a held pin stays untouched | board | not run; `tests/rp2040/gpioprobe` decides it |
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
