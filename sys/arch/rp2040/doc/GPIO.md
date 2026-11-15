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

The union kernel reads the board from its descriptor (`board_id()`), so
one image applies the right row. A request that names a held pin has no
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

## Evidence

| Claim | Class | State |
| --- | --- | --- |
| Pin masks, port mapping and request validation | host | `check-gpio-requests`, six mutants rejected |
| The four kernels compile and link the driver | build | observed |
| A configured output drives its pin; an input reads its level; a held pin stays untouched | board | not run |
