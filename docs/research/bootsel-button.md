# BOOTSEL button and USB re-entry on RP2040, bare metal, no debug probe

Scope: a bare-metal kernel on an RP2040 (Pico), USB cable attached, no SWD probe. Three
questions: reading BOOTSEL at runtime without an SDK, forcing a reboot into the USB
bootloader from software, and what a running (non-SDK) image must expose so `picotool`
can reboot it with no button press.

## Findings

### 1. Reading BOOTSEL at runtime

The button on Pico boards is wired to the QSPI flash chip-select pin (QSPI_SS), not to a
free GPIO. The bootrom itself polls this same pin at power-up ("bootrom button", Section
2.8.1 of the datasheet -- see excerpt below). At runtime, the SIO peripheral exposes the
current electrical state of the six QSPI-bank pins (SCLK, SS, SD0-SD3) through
`SIO_GPIO_HI_IN`, and the QSPI-bank IO controller (`IO_QSPI`) can force that pin's output
enable to Hi-Z independently of its normal peripheral function (XIP/SSI control of flash
chip-select). The `pico-examples` `picoboard/button` sample uses this: it floats CS,
waits briefly, samples the pin, then restores CS. Because flash execute-in-place depends
on CS being driven by the SSI/XIP hardware, the whole sequence must run from RAM with
interrupts off -- flash reads (instruction fetch included) will hang or corrupt if CS is
floating.

Register facts, from the RP2040 datasheet:

- `IO_QSPI_BASE = 0x40018000` (Section 2.19.6.2, "IO - QSPI Bank").
- `GPIO_QSPI_SS_CTRL` is at offset `0x0c` in that block (index 1 of the six QSPI IO
  registers: SCLK=0x04, SS=0x0c, SD0=0x14, SD1=0x1c, SD2=0x24, SD3=0x2c). Full absolute
  address: `0x4001800c`.
- Within `GPIO_QSPI_SS_CTRL` (and every `GPIO_QSPI_*_CTRL` register, they share one
  layout), the `OEOVER` field is bits `[13:12]`, `INOVER` is bits `[17:16]`. `OEOVER`
  encoding: `0x0` NORMAL (output enable driven by the peripheral/funcsel), `0x1` INVERT,
  `0x2` DISABLE (force output disabled -- Hi-Z), `0x3` ENABLE (force output enabled).
  `pico-sdk`'s `enum gpio_override` names value `2` as `GPIO_OVERRIDE_LOW` ("drive
  low/disable output") and value `3` as `GPIO_OVERRIDE_HIGH`; for the `OEOVER` field that
  maps to DISABLE/ENABLE respectively, which is why the example writes
  `GPIO_OVERRIDE_LOW` to `OEOVER` to float the pin.
- `SIO_BASE = 0xd0000000`; `GPIO_HI_IN` is at offset `0x008`, so `SIO_GPIO_HI_IN =
  0xd0000008`. It packs the 6 QSPI pin states; QSPI_SS is bit 1 (`1u << 1`).
- The QSPI_SS net has an on-die pull-up; the Pico's BOOTSEL button pulls it to ground
  when pressed. So with CS floated, bit 1 of `GPIO_HI_IN` reads 1 when not pressed and 0
  when pressed -- the example inverts the read (`!(... & CS_BIT)`) so the returned
  boolean is true only while the button is held.
- Interrupts must stay disabled for the whole float/sample/restore window because an
  interrupt handler resident in flash (or any code touching flash, on this or the other
  core, or the XIP streamer) will fault or stall the moment CS is floating and the SSI is
  not driving it -- flash is unreachable in that window. This is also why the routine
  itself must be linked to run from RAM (`__not_in_flash_func`), and why a plain busy-loop
  delay is used instead of a flash-resident `sleep_us`.
- CS is restored to `OEOVER = NORMAL` (0x0) before returning, handing chip-select control
  back to the SSI/XIP hardware so flash fetches resume working.
- Caveat inherited directly from the upstream comment: this technique does not work
  safely if anything else (the other core, DMA, the XIP streamer) is touching flash
  concurrently, since CS is genuinely shared hardware during the float window.

### 2. Software re-entry to BOOTSEL and behavior of a plain reset

`reset_to_usb_boot` is bootrom function code `'U','B'` (`ROM_TABLE_CODE('U','B')`,
defined in `pico-sdk` as `ROM_FUNC_RESET_USB_BOOT`), looked up through the bootrom
function table exactly like any other ROM entry point (`rom_func_lookup`). Its C
prototype in the SDK is:

```c
void __attribute__((noreturn)) rom_reset_usb_boot(uint32_t usb_activity_gpio_pin_mask,
                                                    uint32_t disable_interface_mask);
```

The datasheet documents the same two arguments for `_reset_to_usb_boot`:

- `gpio_activity_pin_mask`: 0 for no activity LED (as at cold boot), otherwise a single
  bit set selecting the GPIO to drive as a mass-storage activity indicator.
- `disable_interface_mask`: 0 enables both USB interfaces (as at cold boot); 1 disables
  the USB Mass Storage interface; 2 disables the USB PICOBOOT interface.

Mechanism: the datasheet states this call "Resets the RP2040 and uses the watchdog
facility to re-start in BOOTSEL mode" -- it writes the watchdog's upper scratch
registers (magic `0xb007c0d3` in scratch 4, entry point XORed with that magic in scratch
5, stack pointer in scratch 6, entry point in scratch 7 -- the general "Watchdog Boot"
mechanism of Section 2.8.1.1) and triggers a watchdog reset. On the next boot, the
bootrom sees the matching magic in scratch 4, zeroes it (one-shot), and jumps straight
into bootrom USB-boot code instead of running the normal flash-boot decision tree. This
is a full chip reset, not a function call that returns -- hence `noreturn`.

Plain reboot behavior (AIRCR `SYSRESETREQ`, or `watchdog_reboot` with no watchdog-boot
scratch magic set): the datasheet's processor-controlled boot sequence (Section 2.8.1)
runs from the top on every reset that is not a "power up event ... from Rescue DP" and
does not carry the watchdog-boot magic:

> "If watchdog scratch registers set to indicate pre-loaded code exists in SRAM, jump to
> that code ... Check if SPI CS pin is tied low ('bootrom button'), and skip flash boot
> if so. ... If checksum passes, assume what we have loaded is a valid flash second
> stage [and] start executing the loaded code from SRAM."

So: a kernel that calls `watchdog_reboot()` or triggers `AIRCR.SYSRESETREQ` with no
watchdog-boot magic set, and with BOOTSEL not held, causes the bootrom to re-check CS,
find it not pulled low, find a flash image with a valid second-stage checksum, and boot
that same image again -- unconditionally, with no way for the ROM to know the previous
run hung. If the kernel is the thing that's hung (never reaches the reset instruction, or
hangs again identically after reboot), the only way out with just a USB cable is a
power cycle while physically holding BOOTSEL, which forces the "skip flash boot" branch
regardless of what's in flash. There is no other recovery path available from outside
once code execution is stuck and the running image exposes no USB reset interface (see
part 3) -- there is no NMI-style watchdog-independent escape hatch documented in the
bootrom for a hung processor with interrupts disabled or spinning.

BOOTSEL priority at power-up is unconditional: the check is "Check if SPI CS pin is tied
low ... and skip flash boot if so," which runs before flash is even probed for a valid
image. Holding BOOTSEL during power-up (or during a watchdog boot that lacks the
watchdog-boot magic) always wins over whatever is in flash, valid or not, per Section
2.8.1 quoted above.

### 3. picotool and the stdio-USB "reset interface"

`picotool reboot -u` (or any picotool command needing BOOTSEL mode) against a device
that is not already in BOOTSEL mode has exactly two paths, both requiring the running
firmware to advertise something specific -- there is no way to force a reboot on a
device that offers neither:

1. A PICOBOOT vendor interface -- only present when the device is already in BOOTSEL
   mode (running the bootrom's own USB code), not applicable to "kernel is running."
2. A **stdio-USB reset interface**: a USB vendor-class interface with
   `bInterfaceClass = 0xFF`, `bInterfaceSubClass = RESET_INTERFACE_SUBCLASS (0x00)`,
   `bInterfaceProtocol = RESET_INTERFACE_PROTOCOL (0x01)`. This is what `pico_stdio_usb`
   adds to a normal SDK CDC-ACM descriptor set when
   `PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE` is set (the SDK default whenever the
   application doesn't link TinyUSB directly, e.g. any typical `pico_stdio_usb` build).

`picotool`'s device probing (`picoboot_connection.c`) walks the active configuration's
interfaces and matches on class/subclass/protocol only -- it does not care about VID/PID
whitelisting for this path ("Runtime reset interface with thirdparty VID"):

```c
// Runtime reset interface with thirdparty VID
if (!ret) {
    for (int i = 0; i < config->bNumInterfaces; i++) {
        if (config->interface[i].altsetting[0].bInterfaceClass == 0xff &&
            config->interface[i].altsetting[0].bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
            config->interface[i].altsetting[0].bInterfaceProtocol == RESET_INTERFACE_PROTOCOL) {
            return dr_vidpid_stdio_usb;
        }
    }
}
```

Once found, `picotool`'s `reboot_device` (`main.cpp`) issues a class request, recipient
interface, to that interface number, with no data stage:

```c
if (bootsel) {
    if (settings.led >= 0) {
        disable_mask |= (settings.led << 9u) | (settings.active_low << 7u) | (1u << 8u);
    }
    ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                  RESET_REQUEST_BOOTSEL, disable_mask, i, nullptr, 0, 2000);
} else {
    ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                  RESET_REQUEST_FLASH, 0, i, nullptr, 0, 2000);
}
```

`bmRequestType = 0x21` (host-to-device, class, interface), `wIndex = i` (the reset
interface's own interface number), no payload. `bRequest`/`wValue` per request:

- `bRequest = RESET_REQUEST_BOOTSEL (0x01)`, `wValue = disable_mask` -- reboot into
  BOOTSEL/USB-boot mode. `wValue` bits 0-1 are forwarded as the bootrom
  `disable_interface_mask`; when an activity LED is requested, bit 8 set means "GPIO
  specified", bits 9-15 carry the GPIO number, bit 7 is active-low.
- `bRequest = RESET_REQUEST_FLASH (0x02)`, `wValue = 0` -- reboot back into the flashed
  application (ordinary `watchdog_reboot`).

The current `pico-sdk` layout (this logic used to live in
`pico_stdio_usb/reset_interface.c`; as of the present `master` it has been factored into
a standalone `pico_usb_reset` library, still reached automatically through
`pico_stdio_usb`) implements the device side in
`src/rp2_common/pico_usb_reset/usb_reset.c`:

```c
uint16_t usb_reset_interface_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              RESET_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
              RESET_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);
    uint16_t const drv_len = sizeof(tusb_desc_interface_t);
    TU_VERIFY(max_len >= drv_len, 0);
    itf_num = itf_desc->bInterfaceNumber;
    return drv_len;
}

bool usb_reset_interface_control_xfer_cb(uint8_t __unused rhport, uint8_t stage, tusb_control_request_t const * request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->wIndex == itf_num) {
#if PICO_USB_RESET_SUPPORT_RESET_TO_BOOTSEL
        if (request->bRequest == RESET_REQUEST_BOOTSEL) {
            ...
            rom_reset_usb_boot_extra(gpio, (request->wValue & 0x3) | PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK, active_low);
            // does not return
        }
#endif
#if PICO_USB_RESET_SUPPORT_RESET_TO_FLASH_BOOT
        if (request->bRequest == RESET_REQUEST_FLASH) {
            watchdog_reboot(0, 0, PICO_USB_RESET_RESET_TO_FLASH_DELAY_MS);
            return true;
        }
#endif
    }
    return false;
}
```

The interface descriptor macro used to advertise this on the wire, from
`include/pico/usb_reset_tusb.h`:

```c
#define TUD_RPI_RESET_DESC_LEN  9
#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
  TUD_RPI_RESET_DESC_LEN, TUSB_DESC_INTERFACE, _itfnum, 0, 0, \
  TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx
```

i.e. a bare 9-byte standard interface descriptor, class `0xFF`, subclass `0x00`, protocol
`0x01`, zero endpoints -- everything happens over EP0 control transfers. The
class/subclass/protocol/request-number constants themselves live in
`pico/usb_reset_interface.h` (formerly `pico/stdio_usb/reset_interface.h`, now a thin
forwarding header):

```c
// VENDOR sub-class for the reset interface
#define RESET_INTERFACE_SUBCLASS 0x00
// VENDOR protocol for the reset interface
#define RESET_INTERFACE_PROTOCOL 0x01

// CONTROL requests:
// reset to BOOTSEL
#define RESET_REQUEST_BOOTSEL 0x01
// regular flash boot
#define RESET_REQUEST_FLASH 0x02
```

Answer to "does `picotool reboot -u` work against a running non-SDK image with no
PICOBOOT vendor interface": no, not unless that image's USB stack independently
implements this exact contract. Nothing about it is tied to CDC-ACM, TinyUSB, or the SDK
build system -- it is only a USB descriptor shape plus a control-request handler. A
bare-metal kernel with its own USB device stack can add a zero-endpoint vendor interface
with this class/subclass/protocol, answer `SETUP` packets on it with a zero-length status
stage, and on request `0x01` call `reset_to_usb_boot` (the `'U','B'` ROM function, part
2 above) or on request `0x02` do its own reboot-to-flash -- and `picotool` will discover
and drive it exactly as it does an SDK `pico_stdio_usb` build, no BOOTSEL press required.

## Quoted primary sources

### `pico-examples/picoboard/button/button.c` (BSD-3-Clause)
<https://github.com/raspberrypi/pico-examples/blob/master/picoboard/button/button.c>

```c
/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/status_led.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

// This example blinks the Pico LED when the BOOTSEL button is pressed.
//
// Picoboard has a button attached to the flash CS pin, which the bootrom
// checks, and jumps straight to the USB bootcode if the button is pressed
// (pulling flash CS low). We can check this pin in by jumping to some code in
// SRAM (so that the XIP interface is not required), floating the flash CS
// pin, and observing whether it is pulled low.
//
// This doesn't work if others are trying to access flash at the same time,
// e.g. XIP streamer, or the other core.

bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}
```

### RP2040 Datasheet, "2.8.1. Processor Controlled Boot Sequence" and "2.8.1.1. Watchdog
Boot" and Table 168 (Miscellaneous Functions)
<https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>

> "After the hardware controlled boot sequence described in Section 2.7, the processor
> controlled boot sequence starts: ... If watchdog scratch registers set to indicate
> pre-loaded code exists in SRAM, jump to that code ... Check if SPI CS pin is tied low
> ('bootrom button'), and skip flash boot if so. ... If checksum passes, assume what we
> have loaded is a valid flash second stage [and] Start executing the loaded code from
> SRAM (SRAM5). If no valid image found in SPI after 0.5 seconds of attempting to boot,
> drop to USB device boot."

> "2.8.1.1. Watchdog Boot -- Watchdog boot allows users to install their own boot
> handler, and divert control away from the main boot sequence on non-POR/BOR resets. ...
> It recognises the following values written to the watchdog's upper scratch registers:
> Scratch 4: magic number 0xb007c0d3; Scratch 5: Entry point XORed with magic
> -0xb007c0d3 (0x4ff83f2d); Scratch 6: Stack pointer; Scratch 7: Entry point. If either
> of the magic numbers mismatch, watchdog boot does not take place. If the numbers
> match, the Bootrom zeroes scratch 4 before transferring control, so that the behaviour
> does not persist over subsequent reboots."

> Table 168, Miscellaneous Functions: "'U','B' void _reset_to_usb_boot(uint32_t
> gpio_activity_pin_mask, uint32_t disable_interface_mask) -- Resets the RP2040 and uses
> the watchdog facility to re-start in BOOTSEL mode: gpio_activity_pin_mask is provided
> to enable an 'activity light' ... disable_interface_mask may be used to control the
> exposed USB interfaces: 0 To enable both interfaces (as per a cold boot); 1 To disable
> the USB Mass Storage Interface; 2 To disable the USB PICOBOOT Interface."

Register tables, same document, Section 2.19.6.2 ("IO - QSPI Bank") and Section 2.3.1.7
("SIO"):

> "The QSPI Bank IO registers start at a base address of 0x40018000 (defined as
> IO_QSPI_BASE in SDK)." Offset table: "0x0c GPIO_QSPI_SS_CTRL -- GPIO control including
> function select and overrides."

> `GPIO_QSPI_SCLK_CTRL, GPIO_QSPI_SS_CTRL, ..., GPIO_QSPI_SD3_CTRL` register bit layout:
> bits `13:12 OEOVER RW 0x0`, enumerated `0x0 NORMAL`, `0x1 INVERT`, `0x2 DISABLE: disable
> output`, `0x3 ENABLE: enable output`.

> "The SIO registers start at a base address of 0xd0000000 (defined as SIO_BASE in
> SDK)." Offset table: "0x008 GPIO_HI_IN -- Input value for QSPI pins."

### `pico-sdk/src/rp2_common/pico_bootrom/include/pico/bootrom.h` (BSD-3-Clause)
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_bootrom/include/pico/bootrom.h>

```c
void __attribute__((noreturn)) rom_reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask);
static inline void __attribute__((noreturn)) reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask) {
    rom_reset_usb_boot(usb_activity_gpio_pin_mask, disable_interface_mask);
}
```

### `pico-sdk/src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h` (BSD-3-Clause)
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/boot_bootrom_headers/include/boot/bootrom_constants.h>

```c
#define ROM_TABLE_CODE(c1, c2) ((c1) | ((c2) << 8))
...
#define ROM_FUNC_RESET_USB_BOOT                 ROM_TABLE_CODE('U', 'B')
```

### `pico-sdk/src/rp2_common/pico_bootrom/bootrom.c` (BSD-3-Clause)
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_bootrom/bootrom.c>

```c
void __attribute__((noreturn)) rom_reset_usb_boot(uint32_t usb_activity_gpio_pin_mask, uint32_t disable_interface_mask) {
#ifdef ROM_FUNC_RESET_USB_BOOT
    rom_reset_usb_boot_fn func = (rom_reset_usb_boot_fn) rom_func_lookup(ROM_FUNC_RESET_USB_BOOT);
    func(usb_activity_gpio_pin_mask, disable_interface_mask);
#elif defined(ROM_FUNC_REBOOT)
    ...
#endif
}
```

### `pico-sdk/src/common/pico_usb_reset_interface_headers/include/pico/usb_reset_interface.h` (BSD-3-Clause)
<https://github.com/raspberrypi/pico-sdk/blob/master/src/common/pico_usb_reset_interface_headers/include/pico/usb_reset_interface.h>

```c
/*
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// VENDOR sub-class for the reset interface
#define RESET_INTERFACE_SUBCLASS 0x00
// VENDOR protocol for the reset interface
#define RESET_INTERFACE_PROTOCOL 0x01

// CONTROL requests:

// reset to BOOTSEL
#define RESET_REQUEST_BOOTSEL 0x01
// regular flash boot
#define RESET_REQUEST_FLASH 0x02
```

### `pico-sdk/src/rp2_common/pico_usb_reset/usb_reset.c` and
`include/pico/usb_reset_tusb.h` (BSD-3-Clause) -- current location of the logic
historically shipped as `pico_stdio_usb/reset_interface.c`
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_usb_reset/usb_reset.c>
<https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_usb_reset/include/pico/usb_reset_tusb.h>

```c
uint16_t usb_reset_interface_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              RESET_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
              RESET_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);
    uint16_t const drv_len = sizeof(tusb_desc_interface_t);
    TU_VERIFY(max_len >= drv_len, 0);
    itf_num = itf_desc->bInterfaceNumber;
    return drv_len;
}

bool usb_reset_interface_control_xfer_cb(uint8_t __unused rhport, uint8_t stage, tusb_control_request_t const * request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->wIndex == itf_num) {
#if PICO_USB_RESET_SUPPORT_RESET_TO_BOOTSEL
        if (request->bRequest == RESET_REQUEST_BOOTSEL) {
            ...
            rom_reset_usb_boot_extra(gpio, (request->wValue & 0x3) | PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK, active_low);
            // does not return, otherwise we'd return true
        }
#endif
#if PICO_USB_RESET_SUPPORT_RESET_TO_FLASH_BOOT
        if (request->bRequest == RESET_REQUEST_FLASH) {
            watchdog_reboot(0, 0, PICO_USB_RESET_RESET_TO_FLASH_DELAY_MS);
            return true;
        }
#endif
    }
    return false;
}
```

```c
#define TUD_RPI_RESET_DESC_LEN  9
#define TUD_RPI_RESET_DESCRIPTOR(_itfnum, _stridx) \
  TUD_RPI_RESET_DESC_LEN, TUSB_DESC_INTERFACE, _itfnum, 0, 0, \
  TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx
```

### `picotool/picoboot_connection/picoboot_connection.c` (BSD-3-Clause) -- device probe
<https://github.com/raspberrypi/picotool/blob/master/picoboot_connection/picoboot_connection.c>

```c
// Runtime reset interface with thirdparty VID
if (!ret) {
    for (int i = 0; i < config->bNumInterfaces; i++) {
        if (config->interface[i].altsetting[0].bInterfaceClass == 0xff &&
            config->interface[i].altsetting[0].bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
            config->interface[i].altsetting[0].bInterfaceProtocol == RESET_INTERFACE_PROTOCOL) {
            return dr_vidpid_stdio_usb;
        }
    }
}
```

### `picotool/main.cpp` (BSD-3-Clause) -- control transfer picotool sends
<https://github.com/raspberrypi/picotool/blob/master/main.cpp>

```c
if (bootsel) {
    if (settings.led >= 0) {
        disable_mask |= (settings.led << 9u) | (settings.active_low << 7u) | (1u << 8u);
    }
    ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                  RESET_REQUEST_BOOTSEL, disable_mask, i, nullptr, 0, 2000);
} else {
    ret = libusb_control_transfer(dev_handle, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                                  RESET_REQUEST_FLASH, 0, i, nullptr, 0, 2000);
}
```

## What the kernel must implement

To make BOOTSEL readable and USB-bootloader re-entry available -- both with and without a
physical button press -- a bare-metal kernel needs:

1. **Runtime BOOTSEL read** (Pico-board hardware only; on boards where BOOTSEL is wired
   to a normal GPIO instead of QSPI_SS, just read that GPIO): a RAM-resident,
   interrupt-disabled routine that writes `GPIO_OVERRIDE_LOW` (0x2) into the `OEOVER`
   field (bits 13:12) of `GPIO_QSPI_SS_CTRL` at `0x4001800c`, delays a few hundred cycles
   without touching flash, reads bit 1 of `SIO_GPIO_HI_IN` at `0xd0000008` (pressed =
   0), then restores `OEOVER` to `GPIO_OVERRIDE_NORMAL` (0x0) before re-enabling
   interrupts or returning to any flash-resident caller. Must run with both cores'
   flash/XIP access otherwise quiesced for the duration.
2. **A ROM-function trampoline for `reset_to_usb_boot`**: resolve bootrom function code
   `ROM_TABLE_CODE('U','B')` through the bootrom's function-table lookup and call it as
   `void (*)(uint32_t gpio_activity_pin_mask, uint32_t disable_interface_mask)` -- gives
   the kernel a software-triggered, unconditional path into BOOTSEL mode (e.g. from a
   debug console command), independent of the button.
3. **Recognition that a plain `AIRCR.SYSRESETREQ` or bare `watchdog_reboot()` reboots
   back into the same flashed image** whenever BOOTSEL is not held and no watchdog-boot
   scratch magic is set -- so a hang recovery strategy that just resets the chip is not
   sufficient if the hang is deterministic; it either needs the reset path from item 2,
   or physical BOOTSEL-held power cycling remains the fallback of last resort.
4. **A vendor-class "reset interface" in the USB device stack**, to let `picotool`
   reboot the board with no button press and no SDK: a zero-endpoint interface
   descriptor with `bInterfaceClass = 0xFF`, `bInterfaceSubClass = 0x00`,
   `bInterfaceProtocol = 0x01`, appended to the existing USB configuration (bump
   `bNumInterfaces` and total configuration length accordingly); a control-transfer
   handler on that interface number that, on `bmRequestType = 0x21` (class, interface)
   with `bRequest = 0x01` (`RESET_REQUEST_BOOTSEL`), calls the item-2 trampoline with
   `disable_interface_mask = wValue & 0x3` (answer with a zero-length status stage
   before the reset if the stack requires it -- the SDK's own handler does not return in
   this branch), and with `bRequest = 0x02` (`RESET_REQUEST_FLASH`) performs its own
   ordinary reboot-to-flash and returns a normal zero-length status ACK.
