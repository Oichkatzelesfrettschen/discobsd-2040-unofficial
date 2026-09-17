# RP2040 bare-metal USB CDC-ACM device driver -- reference research

Scope: everything needed to write a bare-metal (no pico-sdk runtime, no TinyUSB) USB
CDC-ACM device driver for the RP2040 USB device controller, for a small Unix kernel
whose console runs over the Pico's single USB cable. This document does not contain
a driver; it collects the primary-source facts the driver is written from.

## 1. dev_lowlevel: minimal control+bulk device, low-level register access

Source: `github.com/raspberrypi/pico-examples`, path
`usb/device/dev_lowlevel/{dev_lowlevel.c,dev_lowlevel.h,usb_common.h}`.
Fetched from `raw.githubusercontent.com/raspberrypi/pico-examples/master/usb/device/dev_lowlevel/`.
License: BSD-3-Clause (`Copyright (c) 2020 Raspberry Pi (Trading) Ltd.`, confirmed from
`pico-examples/LICENSE.TXT`, standard 3-clause text).

This is the canonical low-level (no pico_stdio_usb, no tinyusb) example. It talks to the
USB device controller registers directly and is the closest upstream analog to what a
bare-metal kernel driver needs; only `hardware/regs`, `hardware/structs`, `hardware/irq`,
`hardware/resets` type/macro headers from pico-sdk are pulled in, no runtime.

### 1.1 Init sequence (`usb_device_init()`, dev_lowlevel.c lines 183-217)

```c
void usb_device_init() {
    // Reset usb controller
    reset_unreset_block_num_wait_blocking(RESET_USBCTRL);

    // Clear any previous state in dpram just in case
    memset(usb_dpram, 0, sizeof(*usb_dpram));

    // Enable USB interrupt at processor
    irq_set_enabled(USBCTRL_IRQ, true);

    // Mux the controller to the onboard usb phy
    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;

    // Force VBUS detect so the device thinks it is plugged into a host
    usb_hw->pwr = USB_USB_PWR_VBUS_DETECT_BITS | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

    // Enable the USB controller in device mode.
    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;

    // Enable an interrupt per EP0 transaction
    usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;

    // Enable interrupts for when a buffer is done, when the bus is reset,
    // and when a setup packet is received
    usb_hw->inte = USB_INTS_BUFF_STATUS_BITS |
                   USB_INTS_BUS_RESET_BITS |
                   USB_INTS_SETUP_REQ_BITS;

    // Set up endpoints (endpoint control registers)
    usb_setup_endpoints();

    // Present full speed device by enabling pull up on DP
    usb_hw_set->sie_ctrl = USB_SIE_CTRL_PULLUP_EN_BITS;
}
```

Order matters: `RESET_USBCTRL` deasserts the controller's reset (see the RESETS block --
`reset_unreset_block_num_wait_blocking` clears the reset bit and spins on `RESETS_DONE`).
DPRAM (the 4 KiB shared buffer/control RAM) is zeroed before anything touches it. Muxing
and VBUS override must be set before `CONTROLLER_EN`, `INTE` masks and endpoint control
registers are programmed before the pull-up goes live, and the pull-up (`SIE_CTRL_PULLUP_EN`)
is the very last step -- it is what makes the RP2040 visible to the host on the bus, so any
state the host might immediately probe (device address 0 response, EP0 max packet size,
endpoint control registers) must already be correct before this bit is set.

`usb_hw_set`/`usb_hw_clear` are atomic bit-set/bit-clear register aliases:
```c
#define usb_hw_set   ((usb_hw_t *)hw_set_alias_untyped(usb_hw))
#define usb_hw_clear ((usb_hw_t *)hw_clear_alias_untyped(usb_hw))
```
This is the RP2040 "hardware atomic aliasing" scheme common to every APB/AHB peripheral on
the chip: writing to `base + 0x1000` OR-sets the bits you write, `base + 0x2000` AND-clears
them, `base + 0x3000` XOR-toggles them, relative to the plain register at `base + 0x0000`.
A bare-metal driver not using pico-sdk headers must reimplement these offsets manually
against `USBCTRL_REGS_BASE`.

### 1.2 Endpoint setup in DPSRAM

`usb_setup_endpoint()` (dev_lowlevel.c lines 149-164) programs the endpoint control
register for every non-EP0 endpoint:

```c
uint32_t dpram_offset = usb_buffer_offset(ep->data_buffer);
uint32_t reg = EP_CTRL_ENABLE_BITS
             | EP_CTRL_INTERRUPT_PER_BUFFER
             | (ep->descriptor->bmAttributes << EP_CTRL_BUFFER_TYPE_LSB)
             | dpram_offset;
*ep->endpoint_control = reg;
```

`usb_buffer_offset()` reduces a DPRAM pointer to its 16-bit offset from the DPRAM base by
XOR against the `usb_dpram` base pointer (works because DPRAM is exactly a naturally
aligned 4 KiB block, so the high bits of both pointers are identical and XOR clears them):

```c
static inline uint32_t usb_buffer_offset(volatile uint8_t *buf) {
    return (uint32_t) buf ^ (uint32_t) usb_dpram;
}
```

EP0 has no endpoint control register (its buffer is fixed at `ep0_buf_a`/`ep0_buf_b`, single
buffered by convention in this example); `usb_setup_endpoint()` returns early when
`ep->endpoint_control == NULL`.

### 1.3 Setup packet handling

`usb_handle_setup_packet()` (dev_lowlevel.c lines 383-427) reads the 8-byte setup packet
directly out of DPRAM at `usb_dpram->setup_packet` (always the first 8 bytes of DPRAM,
regardless of device/host mode), and always resets EP0 IN's DATA PID to 1 first:
```c
volatile struct usb_setup_packet *pkt =
        (volatile struct usb_setup_packet *) &usb_dpram->setup_packet;
usb_get_endpoint_configuration(EP0_IN_ADDR)->next_pid = 1u;
```
A control transfer's status/data stages always begin with DATA1 per USB 2.0 sec 8.5.3;
resetting `next_pid` on every SETUP guarantees this without per-request bookkeeping.

**SET_ADDRESS** (`usb_set_device_address()`, lines 356-364): the new address is *not*
applied immediately. The device must remain at address 0 to complete the status stage;
`dev_addr_ctrl` is only written to hardware from the EP0 IN completion callback:
```c
void usb_set_device_address(volatile struct usb_setup_packet *pkt) {
    dev_addr = (pkt->wValue & 0xff);
    should_set_address = true;
    usb_acknowledge_out_request();   // sends a 0-length status packet as addr 0
}
...
void ep0_in_handler(uint8_t *buf, uint16_t len) {
    if (should_set_address) {
        usb_hw->dev_addr_ctrl = dev_addr;   // NOW take the new address
        should_set_address = false;
    } else {
        usb_start_transfer(usb_get_endpoint_configuration(EP0_OUT_ADDR), NULL, 0);
    }
}
```
This is the single most important sequencing gotcha for SET_ADDRESS: send the zero-length
status packet as address 0, and only write `dev_addr_ctrl` after that IN transfer's buffer
completes. Writing the address before the status stage lands makes the host's remaining
address-0 status stage go unanswered.

**GET_DESCRIPTOR** (`usb_handle_setup_packet()` dispatches on `wValue >> 8`):
device (`USB_DT_DEVICE` = 1), config (`USB_DT_CONFIG` = 2, and if `wLength` covers the
whole `wTotalLength`, the interface + endpoint descriptors are concatenated after it into
`ep0_buf` before the single `usb_start_transfer` call), and string (`USB_DT_STRING` = 3,
index 0 = language ID array, index >=1 = UTF-16LE string built on the fly by
`usb_prepare_string_descriptor()`).

**SET_CONFIGURATION**: trivial in this example (one configuration only) -- acknowledge
with a zero-length status packet and set a `configured` flag:
```c
void usb_set_device_configuration(volatile struct usb_setup_packet *pkt) {
    usb_acknowledge_out_request();
    configured = true;
}
```

### 1.4 IRQ handler flow (`isr_usbctrl`, dev_lowlevel.c lines 494-524)

```c
void isr_usbctrl(void) {
    uint32_t status = usb_hw->ints;
    uint32_t handled = 0;

    if (status & USB_INTS_SETUP_REQ_BITS) {
        handled |= USB_INTS_SETUP_REQ_BITS;
        usb_hw_clear->sie_status = USB_SIE_STATUS_SETUP_REC_BITS;
        usb_handle_setup_packet();
    }

    if (status & USB_INTS_BUFF_STATUS_BITS) {
        handled |= USB_INTS_BUFF_STATUS_BITS;
        usb_handle_buff_status();
    }

    if (status & USB_INTS_BUS_RESET_BITS) {
        handled |= USB_INTS_BUS_RESET_BITS;
        usb_hw_clear->sie_status = USB_SIE_STATUS_BUS_RESET_BITS;
        usb_bus_reset();
    }

    if (status ^ handled) {
        panic("Unhandled IRQ 0x%x\n", (uint) (status ^ handled));
    }
}
```
Note each branch must clear its *source* status bit in `sie_status` (via the clear-alias
register) as well as being implicitly cleared from `ints`/`intr` by virtue of the source
condition going away -- `SETUP_REC` and `BUS_RESET` in `SIE_STATUS` are write-1-to-clear
and are the actual latched flags; `INTS`/`INTR` are just masked/raw views over them and
`BUFF_STATUS` (see 1.5) has its own per-endpoint clear register.

`usb_bus_reset()` resets device state on a bus reset condition: address back to 0,
`should_set_address = false`, `usb_hw->dev_addr_ctrl = 0`, `configured = false`. A real
driver must also reset endpoint DATA PID toggles and any in-flight transfer state here,
since the host is guaranteed to restart enumeration from SETUP/DATA0.

### 1.5 Buffer-status handling and bulk transfer start/completion

`usb_hw->buf_status` is a 32-bit bitmap, one bit per (endpoint, direction) pair, bit `2*n`
= EPn IN done, bit `2*n+1` = EPn OUT done (see struct comment in 1.7 below: bit 0 = EP0_IN,
bit 1 = EP0_OUT, bit 2 = EP1_IN, bit 3 = EP1_OUT, ... bit 30 = EP15_IN, bit 31 = EP15_OUT).
`usb_handle_buff_status()` walks the set bits, clears each one via the atomic clear alias
(`usb_hw_clear->buf_status = bit`) *before* dispatching, then calls that endpoint's handler:

```c
static void usb_handle_buff_status() {
    uint32_t buffers = usb_hw->buf_status;
    uint32_t remaining_buffers = buffers;
    uint bit = 1u;
    for (uint i = 0; remaining_buffers && i < USB_NUM_ENDPOINTS * 2; i++) {
        if (remaining_buffers & bit) {
            usb_hw_clear->buf_status = bit;
            usb_handle_buff_done(i >> 1u, !(i & 1u));   // IN for even i, OUT for odd i
            remaining_buffers &= ~bit;
        }
        bit <<= 1u;
    }
}
```

A transfer is *started* by writing the buffer control register for that endpoint
(`usb_start_transfer`, lines 238-260):
```c
void usb_start_transfer(struct usb_endpoint_configuration *ep, uint8_t *buf, uint16_t len) {
    assert(len <= 64);
    uint32_t val = len | USB_BUF_CTRL_AVAIL;
    if (ep_is_tx(ep)) {
        memcpy((void *) ep->data_buffer, (void *) buf, len);
        val |= USB_BUF_CTRL_FULL;
    }
    val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
    ep->next_pid ^= 1u;
    *ep->buffer_control = val;
}
```
For an IN (TX) endpoint the caller copies data into the DPRAM buffer first, sets `FULL`
(data is present and ready to send) and `LEN`, then `AVAIL` (hardware may now use this
buffer) last conceptually, though here it's built as one value and written atomically --
the important invariant is that `FULL`/`LEN`/`PID` must all be valid *before or in the
same write as* `AVAIL`, since the SIE can act on the buffer the instant `AVAIL` is set.
For an OUT (RX) endpoint, `FULL` is left clear and only `LEN` (the max the host may send)
and `AVAIL` are set -- hardware sets `FULL` itself once it has written received data into
the buffer and generates the buff-status IRQ.

Completion, from the bulk echo endpoints in this example (EP1 OUT / EP2 IN):
```c
void ep1_out_handler(uint8_t *buf, uint16_t len) {
    struct usb_endpoint_configuration *ep = usb_get_endpoint_configuration(EP2_IN_ADDR);
    usb_start_transfer(ep, buf, len);              // echo back on EP2 IN
}
void ep2_in_handler(uint8_t *buf, uint16_t len) {
    usb_start_transfer(usb_get_endpoint_configuration(EP1_OUT_ADDR), NULL, 64);  // re-arm RX
}
```
`usb_handle_ep_buff_done()` extracts the actually-transferred length from the low bits of
the buffer control register (`USB_BUF_CTRL_LEN_MASK`) before calling the handler -- for an
OUT endpoint this is how many bytes the host actually sent, which may be less than the
`len` armed for reception.

This example only ever sends/receives a single 64-byte packet per `usb_start_transfer`
call (`assert(len <= 64)`); multi-packet transfers on one endpoint require either manual
re-arming per packet (as the echo handlers do) or the double-buffered / multi-buffer modes
of `EP_CTRL_DOUBLE_BUFFERED_BITS` / `EP_CTRL_INTERRUPT_PER_DOUBLE_BUFFER`, which this
example does not use.

### 1.6 Descriptor structures (usb_common.h, BSD-3-Clause)

```c
struct usb_setup_packet {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __packed;

struct usb_device_descriptor { uint8_t bLength; uint8_t bDescriptorType; uint16_t bcdUSB;
    uint8_t bDeviceClass; uint8_t bDeviceSubClass; uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0; uint16_t idVendor; uint16_t idProduct; uint16_t bcdDevice;
    uint8_t iManufacturer; uint8_t iProduct; uint8_t iSerialNumber;
    uint8_t bNumConfigurations; } __packed;

struct usb_configuration_descriptor { uint8_t bLength; uint8_t bDescriptorType;
    uint16_t wTotalLength; uint8_t bNumInterfaces; uint8_t bConfigurationValue;
    uint8_t iConfiguration; uint8_t bmAttributes; uint8_t bMaxPower; } __packed;

struct usb_interface_descriptor { uint8_t bLength; uint8_t bDescriptorType;
    uint8_t bInterfaceNumber; uint8_t bAlternateSetting; uint8_t bNumEndpoints;
    uint8_t bInterfaceClass; uint8_t bInterfaceSubClass; uint8_t bInterfaceProtocol;
    uint8_t iInterface; } __packed;

struct usb_endpoint_descriptor { uint8_t bLength; uint8_t bDescriptorType;
    uint8_t bEndpointAddress; uint8_t bmAttributes; uint16_t wMaxPacketSize;
    uint8_t bInterval; } __packed;
```
Plus the bmRequestType/type/recipient masks, transfer-type constants (control=0, iso=1,
bulk=2, interrupt=3), descriptor type bytes (DEVICE=1, CONFIG=2, STRING=3, INTERFACE=4,
ENDPOINT=5), and standard request codes (SET_ADDRESS=5, GET_DESCRIPTOR=6,
SET_CONFIGURATION=9, etc.) needed to parse any control transfer.

## 2. Register and bit-constant reference (from pico-sdk, BSD-3-Clause)

Source: `github.com/raspberrypi/pico-sdk`, paths
`src/rp2040/hardware_regs/include/hardware/regs/usb.h` (register offsets, full bit layout,
auto-generated from the RP2040 register list),
`src/rp2040/hardware_structs/include/hardware/structs/usb.h` (the `usb_hw_t` struct laid
over `USBCTRL_REGS_BASE`), and
`src/rp2040/hardware_structs/include/hardware/structs/usb_dpram.h` (the DPRAM layout and
the `USB_BUF_CTRL_*` / `EP_CTRL_*` software constants -- these are **not** raw SVD-style
register bit macros; they are pico-sdk-authored convenience constants layered on top of
the datasheet's documented bit positions). All three files: `Copyright (c) 2021/2024
Raspberry Pi (Trading) Ltd.`, `SPDX-License-Identifier: BSD-3-Clause`.

### 2.1 Base addresses

| Name | Value | Meaning |
|---|---|---|
| `USBCTRL_DPRAM_BASE` / `USBCTRL_BASE` | `0x50100000` | Base of the 4 KiB dedicated USB DPSRAM (setup packet, per-endpoint control/buffer-control registers, and packet data all live here) |
| `USBCTRL_REGS_BASE` | `0x50110000` | Base of the USB controller's memory-mapped register file (`usb_hw_t`) |
| Hardware set/clear/xor alias offsets | `+0x1000` / `+0x2000` / `+0x3000` from any peripheral's plain base | RP2040-wide atomic bit-set/clear/toggle aliasing; applies to `USBCTRL_REGS_BASE` too |

### 2.2 `usb_hw_t` registers of interest (offsets from `USBCTRL_REGS_BASE`)

| Register | Offset | Key bits (name = value) |
|---|---|---|
| `MAIN_CTRL` | `0x40` | `CONTROLLER_EN` = `0x00000001`; `HOST_NDEVICE` = `0x00000002` (0 = device mode); `SIM_TIMING` = `0x80000000` |
| `SIE_CTRL` | `0x4c` | `PULLUP_EN` = `0x00010000`; `RPU_OPT` = `0x00020000` (0 = 1k2 pull-up, 1 = 2k3); `EP0_INT_1BUF` = `0x20000000`; `EP0_INT_2BUF` = `0x10000000`; `EP0_INT_STALL` = `0x80000000`; `EP0_INT_NAK` = `0x08000000`; `EP0_DOUBLE_BUF` = `0x40000000`; `RESUME` = `0x00001000` (remote wakeup) |
| `SIE_STATUS` | -- | `BUS_RESET` = `0x00080000`; `SETUP_REC` = `0x00020000`; `TRANS_COMPLETE` = `0x00040000`; `CONNECTED` = `0x00010000`; `SUSPENDED` = `0x00000010`; `VBUS_DETECTED` = `0x00000001`; `LINE_STATE` = bits `[3:2]`; `SPEED` = bits `[9:8]` (host mode only); write-1-to-clear |
| `INT_EP_CTRL` | `0x54` | `INT_EP_ACTIVE` bits `[15:1]` -- host-mode interrupt-endpoint enables, not used in device mode |
| `BUFF_STATUS` | `0x58` | one bit per (EP, dir): bit0 = EP0_IN, bit1 = EP0_OUT, bit2 = EP1_IN, bit3 = EP1_OUT, ... bit `2n` = EPn_IN, bit `2n+1` = EPn_OUT, up to EP15; write-1-to-clear via the atomic clear alias |
| `EP_ABORT` / `EP_ABORT_DONE` | -- | per-(EP,dir) bitmaps to force-abandon a buffer that the SIE would otherwise still be allowed to use (needed to safely cancel a pending OUT reception) |
| `EP_STALL_ARM` | -- | bits `EP0_IN`/`EP0_OUT` only -- must be set alongside the `STALL` bit in that endpoint's buffer control register for EP0 (the only endpoint where STALL needs this two-step arm) |
| `USB_MUXING` | -- | `TO_PHY` = `0x00000001`; `TO_EXTPHY` = `0x00000002`; `TO_DIGITAL_PAD` = `0x00000004`; `SOFTCON` = `0x00000008` |
| `USB_PWR` | -- | `VBUS_EN` = `0x00000001`; `VBUS_EN_OVERRIDE_EN` = `0x00000002`; `VBUS_DETECT` = `0x00000004`; `VBUS_DETECT_OVERRIDE_EN` = `0x00000008`; `OVERCURR_DETECT` = `0x00000010`; `OVERCURR_DETECT_EN` = `0x00000020` |
| `INTE` / `INTF` / `INTS` / `INTR` | `0x90` (INTE) et al. | `BUS_RESET` = `0x00001000`; `SETUP_REQ` = `0x00010000`; `BUFF_STATUS` = `0x00000010`; `DEV_SOF` = `0x00020000`; `DEV_SUSPEND` = `0x00004000`; `DEV_RESUME_FROM_HOST` = `0x00008000`; `DEV_CONN_DIS` = `0x00002000`; `TRANS_COMPLETE` = `0x00000008`; `ERROR_DATA_SEQ` = `0x00000020`; `ERROR_RX_OVERFLOW` = `0x00000080`; `ERROR_RX_TIMEOUT` = `0x00000040`; `ERROR_BIT_STUFF` = `0x00000100`; `ERROR_CRC` = `0x00000200`; `EP_STALL_NAK` = `0x00080000` |
| `ADDR_ENDP` (`dev_addr_ctrl`) | `0x00` | `ADDRESS` bits `[6:0]` -- device address, write only after the SET_ADDRESS status stage completes |

### 2.3 DPRAM layout (`usb_device_dpram_t`, offsets from `0x50100000`)

```c
typedef struct {
    volatile uint8_t setup_packet[8];              // offset 0x000, always the setup packet
    struct { io_rw_32 in, out; } ep_ctrl[15];       // offset 0x008, endpoint control, EP1..EP15
    struct { io_rw_32 in, out; } ep_buf_ctrl[16];   // offset 0x080, buffer control, EP0..EP15
    uint8_t ep0_buf_a[0x40];                        // offset 0x100, EP0 buffer (single-buffered)
    uint8_t ep0_buf_b[0x40];                        // offset 0x140, EP0's second buffer (unused single-buffered)
    uint8_t epx_data[USB_DPRAM_MAX - 0x180];        // offset 0x180.., free for EP1..EP15 packet data
} usb_device_dpram_t;
```
`ep_ctrl[]` is indexed starting at EP1 (EP0 has no control register -- its buffer location
is fixed), `ep_buf_ctrl[]` is indexed starting at EP0. Total DPRAM is 4096 bytes
(`USB_DPRAM_SIZE` = `0x1000`); `epx_data` therefore has `4096 - 0x180` = 3712 bytes
available to be carved up among all non-EP0 endpoint buffers, e.g. 58 possible 64-byte
buffers, though double-buffered/large endpoints consume more.

### 2.4 Buffer control register bits (`USB_BUF_CTRL_*`, one register per (EP, direction))

| Constant | Value | Meaning |
|---|---|---|
| `USB_BUF_CTRL_FULL` | `0x00008000` | For IN: data buffer holds valid TX data. For OUT: hardware sets this when it has written received data into the buffer (host->device data is present) |
| `USB_BUF_CTRL_LAST` | `0x00004000` | This is the last buffer of the transfer (relevant to double-buffered mode; end-of-transfer marker) |
| `USB_BUF_CTRL_DATA1_PID` | `0x00002000` | Use DATA1 PID for this transaction |
| `USB_BUF_CTRL_DATA0_PID` | `0x00000000` | Use DATA0 PID for this transaction (bit clear) |
| `USB_BUF_CTRL_SEL` | `0x00001000` | Double-buffered mode: select buffer B instead of buffer A |
| `USB_BUF_CTRL_STALL` | `0x00000800` | STALL this endpoint (EP0 additionally needs `EP_STALL_ARM`) |
| `USB_BUF_CTRL_AVAIL` | `0x00000400` | Buffer is available for the SIE to use -- must be set (in the same or a later write than FULL/LEN/PID) to actually start the transaction; hardware clears it on completion |
| `USB_BUF_CTRL_LEN_MASK` | `0x000003FF` | Low 10 bits: transfer length in bytes (max 1023, but full-speed bulk/interrupt/control packets never exceed 64) |

### 2.5 Endpoint control register bits (`EP_CTRL_*`, EP1..EP15 only)

| Constant | Value | Meaning |
|---|---|---|
| `EP_CTRL_ENABLE_BITS` | `1u << 31` | Endpoint enabled |
| `EP_CTRL_DOUBLE_BUFFERED_BITS` | `1u << 30` | Endpoint uses double buffering |
| `EP_CTRL_INTERRUPT_PER_BUFFER` | `1u << 29` | Raise BUFF_STATUS interrupt on every completed buffer |
| `EP_CTRL_INTERRUPT_PER_DOUBLE_BUFFER` | `1u << 28` | Raise interrupt only after both buffers of a double-buffered pair complete |
| `EP_CTRL_INTERRUPT_ON_NAK` | `1u << 16` | Raise interrupt when this endpoint NAKs |
| `EP_CTRL_INTERRUPT_ON_STALL` | `1u << 17` | Raise interrupt when this endpoint STALLs |
| `EP_CTRL_BUFFER_TYPE_LSB` | `26` | Shift for the 2-bit transfer type field (control=0/iso=1/bulk=2/interrupt=3, same encoding as `bmAttributes` low 2 bits) |
| bits `[15:0]` | -- | DPRAM byte offset of this endpoint's data buffer (from `usb_buffer_offset()`, i.e. buffer address XOR DPRAM base) |

### 2.6 USB packet size limit

Full speed (the RP2040 device controller is full-speed only, 12 Mbit/s) caps every
endpoint's `wMaxPacketSize` at 64 bytes for control, bulk, and interrupt transfers
(`USB_MAX_PACKET_SIZE` = 64 in `usb_dpram.h`); isochronous full-speed packets can be up to
1023 bytes (`USB_MAX_ISO_PACKET_SIZE` = 1023), irrelevant to CDC-ACM. A transfer larger
than 64 bytes on a bulk/control/interrupt endpoint must be split into multiple 64-byte (or
smaller final) packets, each individually armed via the buffer control register -- the
dev_lowlevel example sidesteps this entirely by asserting `len <= 64` and never sending
more than one packet per `usb_start_transfer()` call.

## 3. CDC-ACM class layer

### 3.1 What CDC-ACM needs beyond dev_lowlevel

dev_lowlevel presents one vendor-specific (`bInterfaceClass = 0xFF`) interface with two
bulk endpoints and no class requests. CDC-ACM instead needs, per USB CDC 1.2 (class code)
and CDC PSTN subclass 1.2 (ACM model) specifications published by usb.org:

- **Two interfaces** in one configuration: a Communications Class Interface (class `0x02`,
  subclass `0x02` = Abstract Control Model, protocol `0x01` = AT commands, or `0x00` for
  "vendor" / no specific protocol) and a Data Class Interface (class `0x0A`, subclass
  `0x00`, protocol `0x00`).
- The Communications interface carries a block of **class-specific (CS_INTERFACE, type
  `0x24`) functional descriptors** immediately after the standard interface descriptor:
  - **Header Functional Descriptor** (subtype `0x00`): `bLength=5, bDescriptorType=0x24,
    bDescriptorSubtype=0x00, bcdCDC` (2 bytes, e.g. `0x0110` for CDC 1.10).
  - **Call Management Functional Descriptor** (subtype `0x01`): `bLength=5,
    bDescriptorType=0x24, bDescriptorSubtype=0x01, bmCapabilities, bDataInterface`.
    `bmCapabilities = 0x00` is legal and simplest: device does not handle call management
    itself and does not require the data class interface for call management.
  - **Abstract Control Management Functional Descriptor** (subtype `0x02`): `bLength=4,
    bDescriptorType=0x24, bDescriptorSubtype=0x02, bmCapabilities`. `bmCapabilities` bit 1
    set (`0x02`) means the device supports SET_LINE_CODING / GET_LINE_CODING /
    SET_CONTROL_LINE_STATE / SERIAL_STATE notification -- the minimum needed for a serial
    console; bit 0 (`Comm_Feature`) and bit 2 (`Break`) can be left clear.
  - **Union Functional Descriptor** (subtype `0x06`): `bLength=5, bDescriptorType=0x24,
    bDescriptorSubtype=0x06, bMasterInterface, bSlaveInterface0` -- binds the Communications
    interface (master) to the Data interface (slave); required so the host driver knows
    the two interfaces are one logical device.
  - A **Telephone Ringer** or other PSTN-specific descriptors are not needed for ACM.
- **One interrupt IN endpoint** on the Communications interface for CDC "notifications"
  (e.g. `SERIAL_STATE`, `0x20`). Linux's `cdc_acm` and Windows' `usbser`/`usbcdc` drivers
  both expect this endpoint to exist even if the device never sends anything on it (a
  console-only device may simply never queue a notification); 8-byte `wMaxPacketSize` is
  conventional and sufficient (a SERIAL_STATE notification's IN payload fits in 10 bytes:
  8-byte notification header + 2 data bytes -- for a device that never sends this, the
  interval only needs to satisfy the descriptor's own `bInterval`, e.g. `0xFF`).
- **Two bulk endpoints** (one IN, one OUT) on the Data interface for the actual TX/RX byte
  stream; `wMaxPacketSize = 64` at full speed.
- An **Interface Association Descriptor (IAD)** is not required when the Communications
  and Data interfaces are contiguous, consecutively numbered, and this is the device's only
  function -- a single-function CDC-ACM device (as opposed to CDC-ACM composited with
  another class on one device) is correctly enumerated by Linux `cdc_acm` and Windows'
  built-in CDC drivers from the Union Functional Descriptor alone, with no IAD. An IAD
  (`bDescriptorType = 0x0B`, class `0xEF`/subclass `0x02`/protocol `0x01` "Interface
  Association Descriptor" convention at the *device* descriptor level, `bDeviceClass =
  0xEF, bDeviceSubClass = 0x02, bDeviceProtocol = 0x01`) becomes necessary only when CDC is
  one of several functions in a composite device, so it is out of scope for a
  console-only device.

### 3.2 Reference bare-metal implementation: `dougsummerville/Bare-Metal-Raspberry-Pi-Pico-2`

Source: `github.com/dougsummerville/Bare-Metal-Raspberry-Pi-Pico-2`, branch `dot`, path
`drivers/usbcdc.c` / `drivers/usbcdc.h`. License: MIT (`Copyright (c) 2025 Douglas H.
Summerville, Binghamton University`, confirmed via `LICENSE.txt` and the GitHub API
`license.spdx_id = "mit"`). This targets the RP2350 (Pico 2) rather than the RP2040, but
the USB device controller and DPRAM layout used are essentially the same IP block and the
CDC descriptor set and class-request handling are directly transferable; treat it as a
worked example of the class layer, not of RP2040 register offsets (cross-check those
against section 2 of this document, sourced from the RP2040-specific pico-sdk headers).

Its configuration descriptor (`drivers/usbcdc.c`, lines 119-177) is a concrete, working
instance of the layout described in 3.1:

```c
static const uint8_t config_descriptor[0x43] = {
0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x02, 0x80, 0x32,      // configuration descriptor
    // Interface 0: Communications, class 0x02 subclass 0x02 (ACM) protocol 0x01 (AT)
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,   // Header FD, bcdCDC = 0x0110
    0x05, 0x24, 0x01, 0x00, 0x01,   // Call Mgmt FD, bmCapabilities=0, bDataInterface=1
    0x04, 0x24, 0x02, 0x02,         // ACM FD, bmCapabilities=0x02 (line coding/state)
    0x05, 0x24, 0x06, 0x00, 0x01,   // Union FD, master=0, slave=1
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0xFF,   // EP1 IN, interrupt, 8B, interval 0xFF
    // Interface 1: Data, class 0x0A subclass 0x02 protocol 0x00
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x02, 0x00, 0x00,
        0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,   // EP2 IN,  bulk, 64B
        0x07, 0x05, 0x03,  0x02, 0x40, 0x00, 0x00,   // EP3 OUT, bulk, 64B
};
```
(Note: this file's own comment mislabels the Data interface's bInterfaceSubClass as
`0x02`; PSTN/data subclass is conventionally `0x00`, but hosts are tolerant of this field
on the Data interface since only the Communications interface's subclass selects the
protocol model -- still, prefer `0x00` in a new driver rather than copying the value
verbatim.)

The device descriptor sets `bDeviceClass = 0x02` (Communications Device Class) at the
*device* level, `bDeviceSubClass = 0x00`, `bDeviceProtocol = 0x00` -- this is the
traditional (pre-IAD) way of telling the host "class information is in the interface
descriptors", appropriate for a single-function, non-composite CDC-ACM device.

### 3.3 Class request handling (0x20/0x21/0x22)

CDC PSTN class requests arrive as SETUP packets with `bmRequestType = 0x21` (host-to-device,
class, interface) or `0xA1` (device-to-host, class, interface); `bRequest` selects:

- **`SET_LINE_CODING` (0x20)**: host-to-device, has a 7-byte OUT data stage:
  `dwDTERate` (4 bytes, LE, baud), `bCharFormat` (1=1 stop bit / etc.), `bParityType`,
  `bDataBits`. A minimal device only needs to accept and discard/store these bytes and ACK
  the status stage -- Linux `cdc_acm` and picocom/minicom-class hosts require this request
  to succeed (not STALL) even if the device ignores the actual line coding, because opening
  `/dev/ttyACMx` always issues SET_LINE_CODING via `termios` configuration.
- **`GET_LINE_CODING` (0x21)**: device-to-host, IN data stage returning the same 7-byte
  structure the device last stored (or a fixed default, e.g. 115200-8-N-1). The reference
  driver's canned answer:
  ```c
  static const uint8_t line_coding[7] = {
      0x00, 0xc2, 0x01, 0x00,  // dwDTERate = 0x0001C200 = 115200 baud, LE
      0x00,                    // bCharFormat = 1 stop bit
      0x00,                    // bParityType = None
      0x08                     // bDataBits = 8
  };
  ```
- **`SET_CONTROL_LINE_STATE` (0x22)**: host-to-device, no data stage, `wValue` bit 0 =
  DTR, bit 1 = RTS. This is what `cdc_acm` sends on `open()`/`close()` of the tty device
  (DTR follows `termios` `HUPCL`/open state) and is exactly the signal a bare-metal driver
  can use to know when a real terminal program has attached to the console, if desired --
  but a minimal implementation only needs to ACK it (empty status stage) unconditionally;
  it must never be STALLed or the host driver treats the device as broken.

All three requests need only a zero-length or short DATA/STATUS stage and are handled from
the SETUP interrupt path exactly like the standard requests in dev_lowlevel's
`usb_handle_setup_packet()`, keyed off `bRequest` (and typically `bmRequestType`) rather
than `wValue`. The reference driver dispatches with a single switch over a merged
`(bRequest << 8) | bmRequestType` value:
```c
switch (setup_packet.wRequest) {
case 0x2021: // SET_LINE_CODING (bmRequestType=0x21, bRequest=0x20)
case 0x2221: // SET_CONTROL_LINE_STATE (bmRequestType=0x21, bRequest=0x22)
    break;    // zero-length status will be sent regardless
case 0x21A1: // GET_LINE_CODING (bmRequestType=0xA1, bRequest=0x21)
    desc_ptr = line_coding;
    desc_len = sizeof(line_coding);
    break;
}
```
A production driver should additionally *store* the bytes from SET_LINE_CODING's OUT data
stage (needs an EP0 OUT buffer capture, not shown minimally above) so GET_LINE_CODING
reflects what was actually set, since some hosts round-trip line coding as a sanity check.

Reference: USB Class Definitions for Communications Devices, Revision 1.2 (usb.org, CDC120
spec); USB CDC PSTN Subclass Specification, Revision 1.2 (usb.org, PSTN120 spec) --
both define these functional descriptors and request codes; treat the exact section/byte
layout in this document as authoritative for what host drivers actually check in practice,
cross-referenced against the two source files above.

## 4. Clocking: PLL_USB and clk_usb

Source: `github.com/raspberrypi/pico-sdk`,
`src/rp2_common/hardware_clocks/include/hardware/clocks.h` (PLL constant defaults) and
`src/rp2_common/pico_runtime_init/runtime_init_clocks.c` (`pll_init()`/`clock_configure_undivided()`
call sites). BSD-3-Clause.

The USB device controller's SIE requires its bus clock, `clk_usb`, to run at exactly
48 MHz. On a standard Pico with a 12 MHz crystal (XOSC), the default pico-sdk boot path
derives this via `PLL_USB`:

| Parameter | Value | Note |
|---|---|---|
| XOSC (reference) frequency | 12 MHz | `XOSC_MHZ` |
| `PLL_USB_REFDIV` | 1 | Input reference divider |
| `PLL_USB_VCO_FREQ_HZ` | `1200 * MHZ` = 1,200,000,000 Hz | VCO must land in PLL's valid range (roughly 750 MHz-1600 MHz on RP2040) |
| Implied `FBDIV` | 100 | `FBDIV = VCO / (XOSC / REFDIV) = 1200 / 12 = 100`, valid range 16-320 |
| `PLL_USB_POSTDIV1` | 5 | First post-divider (range 1-7) |
| `PLL_USB_POSTDIV2` | 5 | Second post-divider (range 1-7), must satisfy `POSTDIV1 >= POSTDIV2` |
| Resulting `clk_usb` | `1200 MHz / 5 / 5 = 48 MHz` | Exactly what the SIE requires |

`clock_configure_undivided(clk_usb, ..., CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
48 * MHZ)` then routes `clk_usb`'s auxiliary source mux to `PLL_USB` output undivided (the
`clk_usb` generator has no integer/fractional divider stage worth using here since the PLL
already lands exactly on 48 MHz). A bare-metal driver not using pico-sdk's `hardware_pll`/
`hardware_clocks` libraries must reimplement: (1) release `RESETS` bits for `pll_usb` and
`clocks`, (2) program `PLL_USB`'s `CS`, `FBDIV_INT`, `PRIM` registers with the REFDIV/FBDIV/
POSTDIV1/POSTDIV2 values above and wait for `CS.LOCK`, (3) set `CLOCKS_CLK_USB_CTRL`'s
`AUXSRC` field to select `PLL_USB` and set `ENABLE`, (4) only then bring the USB controller
out of reset and run `usb_device_init()`-equivalent code -- `clk_usb` must be running and
stable before `USBCTRL_REGS_BASE` and `USBCTRL_DPRAM_BASE` are touched, since the USB
controller's own register/DPRAM bus interface is clocked by `clk_usb`, not `clk_sys`.
(The reference bare-metal driver in section 3.2 targets the RP2350 and configures its USB
PLL to 144 MHz internally before dividing down inside `configure_usbcdc()`; do not copy its
PLL numbers for the RP2040 -- use the RP2040-specific 1200/5/5 = 48 MHz derivation above.)

## 5. Known gotchas and errata

1. **SET_ADDRESS timing** (already detailed in 1.3): the device answers the status stage
   of SET_ADDRESS while still at address 0, and only programs `usb_hw->dev_addr_ctrl` after
   that IN transfer's completion callback fires. Programming the address earlier orphans
   the status stage and the host declares enumeration failure.

2. **DATA0/DATA1 toggling**: every non-isochronous endpoint's buffer control register
   carries an explicit PID selection bit (`USB_BUF_CTRL_DATA1_PID` / `_DATA0_PID`) -- the
   RP2040 does not toggle this automatically; software must alternate it on every
   successfully completed transfer on that endpoint (`ep->next_pid ^= 1u` in
   dev_lowlevel), and must explicitly reset it to DATA1 at the start of each new control
   transfer's data/status stage (per USB 2.0 8.5.3) and on any endpoint whose state is
   invalidated by a bus reset or a STALL clear (CLEAR_FEATURE ENDPOINT_HALT resets the
   toggle to DATA0 per USB 2.0 9.4.5).

3. **64-byte packet limit at full speed** (section 2.6): control/bulk/interrupt endpoints
   cap at 64 bytes/packet; anything larger needs to be split by software into multiple
   armed buffer-control writes, one IRQ-driven step per packet, since there is no hardware
   scatter-gather beyond the optional 2-buffer (double-buffered) mode per endpoint.

4. **clk_usb must be exactly 48 MHz from PLL_USB** (section 4) before the controller is
   taken out of reset; running the SIE off the wrong clock produces bit-level timing errors
   on the bus (framing/bit-stuff/CRC errors, or no bus activity at all), not a clean
   failure mode.

5. **DPRAM must be zeroed before endpoints are configured** -- `usb_device_init()` does
   `memset(usb_dpram, 0, sizeof(*usb_dpram))` immediately after the controller reset and
   before anything else touches DPRAM, because stale buffer-control bits (in particular a
   leftover `AVAIL` bit) left over from a previous run/reset could cause the SIE to
   immediately act on garbage buffer contents/lengths the instant the pull-up goes live.

6. **RP2040-E5 errata -- USB device fails to exit BUS RESET on a busy/broadcasting hub.**
   Source: `pico-sdk` `src/rp2_common/pico_fix/rp2040_usb_device_enumeration/
   rp2040_usb_device_enumeration.c` (BSD-3-Clause, `Copyright (c) 2020 Raspberry Pi
   (Trading) Ltd.`), corroborated by the Raspberry Pi forum thread "[solved] Errata
   RP2040-E5 workaround" and Klipper3d PR #4748/#5552 implementing the same fix. Silicon
   revisions B0 and B1 (i.e. essentially all production RP2040 parts) require **800
   microseconds of continuous linestate-J (idle) after a bus reset** before the SIE will
   transition out of the RESET state into CONNECTED. On a real host controller this
   condition is naturally met, but on some hub/host combinations that interleave other
   traffic (broadcast packets for other downstream devices) during that window, the SIE
   never sees a clean 800 us idle gap and the device gets stuck failing to enumerate. The
   documented workaround (from the pico-sdk source comment, reproduced verbatim):
   > "After coming out of reset, the hardware expects 800us of LS_J (linestate J) time
   > before it will move to the connected state. However on a hub that broadcasts packets
   > for other devices this isn't the case. The plan here is to wait for the end of the
   > bus reset, force an LS_J for 1ms and then switch control back to the USB phy.
   > Unfortunately this requires us to use GPIO15 as there is no other way to force the
   > input path. We only need to force DP as DM can be left at zero. It will be gated off
   > by GPIO logic if it isn't func selected."

   Practically: on `BUS_RESET`, before/instead of relying purely on the SIE's automatic
   connect sequencing, the workaround (a) waits for the bus to actually go to SE0 (both
   D+/D- low, confirming the reset condition itself), (b) temporarily repurposes GPIO15
   (steals it from whatever else uses it, for roughly 1 ms) to directly drive linestate J
   on D+ via the digital pad / IO bank overrides rather than the analog USB PHY, holding it
   there for the full 800 us+ the hardware requires, then (c) hands control back to the
   normal USB PHY path (`muxing.TO_PHY`). This is chip-revision-gated
   (`PICO_RP2040_B0_SUPPORTED || PICO_RP2040_B1_SUPPORTED`, checked at runtime via
   `rp2040_chip_version() == 1`) and is silicon-specific to RP2040 (does not apply to
   RP2350). A bare-metal kernel targeting only known-good hosts/hubs directly wired to the
   Pico's own USB connector may be able to skip this workaround in practice, but any driver
   intended to be robust across arbitrary hub topologies should implement it, since its
   absence manifests as intermittent enumeration failure that looks host-dependent and is
   easy to misdiagnose as a descriptor or timing bug elsewhere in the driver.

   Note: a separate, unrelated defect exists in the RP2040's USB **host**-mode Bulk IN
   DATA0/DATA1 resynchronization (RP2040 fails to ACK+discard a packet carrying an
   unexpected PID per USB 2.0 8.6.4, causing a retransmit deadlock) -- reported in
   `raspberrypi/pico-feedback` issue #394. That defect is host-controller-side, explicitly
   not covered by the documented RP2040-E5 errata entry, and irrelevant to a device-only
   (peripheral) CDC-ACM driver; it is noted here only to avoid confusing it with RP2040-E5.

## 6. Source and license summary

| Source | URL | License |
|---|---|---|
| pico-examples `usb/device/dev_lowlevel/*` | `github.com/raspberrypi/pico-examples/tree/master/usb/device/dev_lowlevel` | BSD-3-Clause (Raspberry Pi (Trading) Ltd., 2020) |
| pico-sdk `hardware/regs/usb.h`, `hardware/structs/usb.h`, `hardware/structs/usb_dpram.h` | `github.com/raspberrypi/pico-sdk/tree/master/src/rp2040/hardware_{regs,structs}/include/hardware/{regs,structs}/` | BSD-3-Clause (Raspberry Pi (Trading) Ltd. / Raspberry Pi Ltd., 2021/2024) |
| pico-sdk `hardware_clocks/include/hardware/clocks.h`, `pico_runtime_init/runtime_init_clocks.c` | `github.com/raspberrypi/pico-sdk/tree/master/src/rp2_common/{hardware_clocks,pico_runtime_init}` | BSD-3-Clause |
| pico-sdk `pico_fix/rp2040_usb_device_enumeration/rp2040_usb_device_enumeration.c` (RP2040-E5 workaround) | `github.com/raspberrypi/pico-sdk/tree/master/src/rp2_common/pico_fix/rp2040_usb_device_enumeration` | BSD-3-Clause (Raspberry Pi (Trading) Ltd., 2020) |
| `dougsummerville/Bare-Metal-Raspberry-Pi-Pico-2`, `drivers/usbcdc.{c,h}` (branch `dot`) | `github.com/dougsummerville/Bare-Metal-Raspberry-Pi-Pico-2/tree/dot/drivers` | MIT (Douglas H. Summerville, Binghamton University, 2025) |
| RP2040-E5 workaround corroboration (forum thread, community PRs) | `forums.raspberrypi.com/viewtopic.php?t=331479`; `github.com/Klipper3d/klipper/pull/4748`, `pull/5552` | N/A (discussion only, no code quoted from these) |
| `raspberrypi/pico-feedback` issue #394 (unrelated host-mode PID defect, noted for disambiguation only) | `github.com/raspberrypi/pico-feedback/issues/394` | N/A (issue tracker, no code quoted) |
| USB Class Definitions for Communications Devices, Rev. 1.2; USB CDC PSTN Subclass Spec, Rev. 1.2 | usb.org (CDC120 / PSTN120 specifications) | Public specification, not source code |

All code excerpted or quoted from BSD-3-Clause and MIT sources above is compatible with a
BSD-licensed driver; no GPL or other copyleft source was consulted for register-level or
descriptor-level facts in this document.
