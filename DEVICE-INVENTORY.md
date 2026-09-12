# Attached-device inventory

## No Raspberry Pi 3B+ is attached

This host carries no Raspberry Pi 3B+ on any interface. Three independent
checks close the question.

USB enumeration lists one Raspberry Pi vendor device, `2e8a:000a`, which is a
Raspberry Pi Pico. A 3B+ cannot appear on a host USB bus at all: its BCM2837B0
OTG port terminates in the onboard LAN7515 hub and ethernet controller, so the
board has no gadget mode and presents no USB device role.

No USB-serial adapter and no SD card reader are present. `/dev/ttyUSB*` does
not exist, and `lsblk` lists only the two NVMe drives and zram, so neither a
serial-header console nor a card-reader path to a 3B+ exists.

A ping sweep of 10.0.0.0/24 followed by a neighbor-table read resolved 37
hosts and matched no Raspberry Pi OUI among `b8:27:eb`, `dc:a6:32`, `e4:5f:01`,
`28:cd:c1`, `d8:3a:dd`, and `2c:cf:67`. Four of those neighbors carry
locally-administered addresses, `0e:aa:65:bb:60:e6`, `1e:04:87:c7:6d:87`,
`9a:ae:e6:a4:57:f7`, and `aa:26:92:09:80:52`, which an OUI match cannot
classify. None answers on port 22, and Raspberry Pi OS uses the burned-in
OUI on both wired and wireless interfaces rather than a randomized address,
so none of the four is a Pi. Neither `raspberrypi.local` nor any Raspberry
Pi mDNS advertisement resolves, which is the check that holds independently
of the hardware address.

The second wired interface, `enp10s0`, reports `Link detected: no`, so no
board sits direct-attached on it.

## What is attached: a Raspberry Pi Pico running DiscoBSD

| Property | Value | Source |
|---|---|---|
| USB ID | 2e8a:000a | `/sys/bus/usb/devices/1-3/idVendor`, `idProduct` |
| Manufacturer string | DiscoBSD | sysfs `manufacturer` |
| Product string | DiscoBSD RP2040 console | sysfs `product` |
| USB serial | rp2040 | sysfs `serial` |
| USB version, speed | 2.00, 12 Mbit/s full speed | sysfs `version`, `speed` |
| Max bus power | 250 mA | sysfs `bMaxPower` |
| Bus, device address | 1, 47 | sysfs `busnum`, `devnum` |
| Host console node | /dev/ttyACM0, group uucp | `ls -l` |
| Board silkscreen | Raspberry Pi Pico, (C) 2020 | DEVICE.md |
| Chip | RP2040, silicon revision B2 | DEVICE.md, `picotool info -d` |
| Flash part | Winbond W25Q16JV, 2 MiB | DEVICE.md, JEDEC `ef 40 15` |
| Flash unique ID | e66488c15f098435 | DEVICE.md, opcode 0x4b |

## Running system

`uname -a` on the board returns `DiscoBSD pico 2.7 PICO#1 rp2040`. The system
boots to a root shell on the USB CDC-ACM console. Three processes run: `init`,
`update`, and the login shell on the USB tty.

The root filesystem is `/dev/fl0a` mounted asynchronous, 932 KB total with
838 KB used, 89 percent full and 94 KB free. `/dev` exposes `fl0`, `fl0a`, and
`fl1` for flash, `swap`, `temp0` through `temp2`, `klog`, `kmem`, `mem`, and
`ttyUSB0` alongside the standard console and null devices.

`/usr/bin` holds 30 entries including `awk`, `sed`, `sort`, `grep`, `find`,
`picoc`, and `xargs`. `/etc/motd` still reads `DiscoBSD ?.? (UNKNOWN)`.

## DEVICE.md rows that are now stale

`~/Github/rpi/DEVICE.md` records "Bootable image in flash: none" and describes
the board enumerating as `2e8a:0003 RP2 Boot` in BOOTSEL with an `RPI-RP2`
volume. The board now enumerates as `2e8a:000a` and runs DiscoBSD from flash,
so those rows describe a superseded state. That file is outside any git
repository, and correcting it is a separate task.

## RP2040 fixed specification

Every figure below comes from `~/Github/rpi/docs/rp2040/rp2040-datasheet.txt`.

| Item | Value | Datasheet location |
|---|---|---|
| Cores | Dual Arm Cortex-M0+ at 133 MHz | key features, line 498 |
| SRAM | 264 KB in six independent banks, single address region | lines 498, 8891 |
| External flash | QSPI, up to 16 MB, XIP window at 0x10000000 | key features |
| DMA channels | 12, registers CH0 through CH11; the block supports up to 16 | section 2.5, line 8811 |
| GPIO | 30 pins, 4 usable as analog inputs | key features |
| ADC | 4 channels, 12-bit, 500 ksps, internal temperature sensor | line 572 |
| PWM | 8 slices, each driving two outputs, 16 channels total | lines 802, 39173 |
| PIO | 2 blocks, 8 state machines total | key features, line 23378 |
| Serial | 2 UART, 2 SPI, 2 I2C | key features |
| USB | 1.1 controller and PHY, host and device | key features |
| Other blocks | interpolators and an integer divider | line 502 |
| Core supply | on-chip LDO, 1.1 V nominal, 100 mA max | line 699 |
| PLLs | 2, for USB and core clocks | key features |
| Package | QFN-56 | line 641 |
| Process | 40 nm | line 491 |

The Pico carries no MMU, no wireless module, and 26 of the 30 GPIO reach the
header.
