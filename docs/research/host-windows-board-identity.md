# discobsd-host on Windows 11: the board was not found

Date: 2026-09-16. Host: Windows 11 Pro 26200, Python 3.13, Chrome.
Board: Raspberry Pi Pico running the DiscoBSD 2.7 image, attached over
its own USB cable. Port fix: discobsd-2040-unofficial PR #81
(distrib/rp2040/host, discobsd-host 1.0.6).

## What Windows shows

The kernel's composite device enumerates three interfaces. Device
Manager (Get-PnpDevice) lists:

| interface | class | name | status |
| --- | --- | --- | --- |
| USB\VID_2E8A&PID_000A\RP2040 | USB | USB Composite Device | OK |
| MI_00 | Ports | USB Serial Device (COM3) | OK, inbox usbser.sys |
| MI_02 | (none) | Reset | Error, no driver |

The CDC-ACM console binds to the inbox driver with no install, as the
host README says. The Reset interface is the picotool vendor interface;
it needs WinUSB (picotool's installer or Zadig) and only picotool cares.
The console does not depend on it.

## The defect

`discobsd-term.exe --list` from the host-v1.0.5 release zip printed
nothing and exited 1, while `DISCOBSD_PORT=COM3 discobsd-term.exe
--probe` reached the board and read the login banner. pyserial on
Windows reported the port as:

    ('COM3', 0x2e8a, 0x000a, 'RP2040', product=None,
     hwid='USB VID:PID=2E8A:000A SER=RP2040')

Windows takes the serial number from the PnP device instance ID, which
the PnP manager upper-cases, so the string is "RP2040" not "rp2040";
usbser.sys exposes no product string, so the "DiscoBSD" fallback had
nothing to match either. ports._is_board compared the serial number
exactly and rejected the board. Linux and macOS read the descriptor
directly and return "rp2040", which is why CI's three-platform test job
never saw it: the test only carried the lower-case form.

## The fix

_is_board lower-cases the serial number before comparing. The test
carries the exact attributes Windows reported. The README's Windows row
says what Device Manager shows and that the driverless Reset interface
is picotool's concern. Version 1.0.6.

Verified on this host: `discobsd-term.exe --list` prints COM3 and
`discobsd-web.exe` serves http://127.0.0.1:7681/ from the 1.0.6 zip
built by the PR's windows job; login as operator, `ls /`, `uname -a`
and `hostname` answered in Chrome through the page.

## Observed along the way

- Characters typed in the first second or two after `login:` accepts
  the name are dropped: the tty flushes typeahead when the shell opens
  it. "uname -a" became "me -a". Expected 2.11BSD behavior; wait for
  the prompt.
- `me -a; ls /` printed "me: not found" and did not run `ls /`. bin/sh
  stops a `;` list at a command that is not found. Not investigated.
- Chrome's screenshot of the page shows a white strip right of the
  80x24 grid; the computed html and body backgrounds are #000 and the
  strip is the capture, not the page.
