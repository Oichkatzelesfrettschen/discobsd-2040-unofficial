# Windows 11 and the board, round two: shell lists, typeahead, BOOTSEL, WinUSB

Date: 2026-09-16. Follows host-windows-board-identity.md, which fixed
host-side discovery (PR #81). This round fixes what the first session
observed on the board and what stood between Windows and picotool.
Port PR: discobsd-pico-unofficial #82.

## 1. bin/sh stopped a list at a command not found

`me -a; ls /` printed "me: not found" and never ran `ls /`. In
bin/sh/xec.c the NOTFOUND branch of a simple command called failure(),
which is exitsh(): in an interactive shell a longjmp to the prompt, in a
script the end of the script. POSIX XCU 2.8.1 lists "command not found"
among the errors an interactive shell shall not exit on, and 2.9.1 gives
it status 127 (126 for found but not executable).

Fix: cmdfail() in error.c prints the same diagnostic, sets exitval, and
returns; xec.c calls it in place of failure(). A forked child (a
pipeline stage, a subshell) and a shell under set -e still go through
failure() and exit with the status, as before. Four rows in
bin/sh/tests/posix-sh.sh: the `;` list continues with `$?` 127, `&&`
and `||` take the status, and set -e is an xfail beside the shell's
existing set -e xfail (opt_e_exits_on_error). The harness runs 110
passes, 0 failures, 15 xfails on WSL Ubuntu 24.04 with gcc-multilib.

On the board: `nosuchcmd -a; echo after $?; ls /sbin` prints
"nosuchcmd: not found", "after 127", and the listing.

## 2. Typeahead through login vanished

A command typed during the motd and profile was lost. Two flushes, one
per program:

- usr.bin/login/login.c set the erase and kill characters with
  TIOCSETP. sys/kern/tty.c flushes the input queue on every TIOCSETP
  (the 4.3BSD contract: SETP flushes, SETN does not; both also flush
  when RAW changes). Nothing in login's call changes RAW, so TIOCSETN
  sets the same modes and keeps the queue. stty already used TIOCSETN.
- bin/sh/edit.c, the line editor, entered and left CBREAK with TIOCSETP
  at every prompt. After the login fix the typed line was echoed by the
  kernel and then discarded here: the echo happened while login ran,
  the flush when the shell drew its first prompt. The editor never sets
  RAW, so TIOCSETN works in both directions: into CBREAK the kernel
  concatenates the pending canonical line onto the raw queue the editor
  reads (tty.c, the CBREAK branch of TIOCSETN), and back to cooked it
  sets PENDIN so the raw bytes are re-run through canonical processing.

On the board, after both fixes: "operator", Return, "uname -a", Return
sent back to back from the browser; the motd printed, the first prompt
appeared with "uname -a" already on it, and the kernel banner line
followed.

## 3. `bootloader` did nothing on this port

sbin/reboot answers to the name bootloader with RB_HALT|RB_BOOTLOADER,
but the rp2040 boot() ignored the flag and halted. It now looks up the
boot ROM's reset_usb_boot (ROM code 'U','B', datasheet 2.8.3.1.3) and
calls it with no LED and no interface disabled, after the sync and a
console drain. The board reappears as the RPI-RP2 mass-storage device
and takes a UF2 by file copy, with no driver and no tool on the host.
adminbox gained the alias, the manifest links /sbin/bootloader, the
standalone sbin/reboot build makes the link too, and reboot(8) has a
paragraph.

On the board: `su`, `bootloader` printed "killing processes... done"
and Windows mounted D: as RPI-RP2 (134 MB, the ROM's fake FAT).

## 4. Windows had no driver for the reset interface

Device Manager showed interface 2, "Reset", with error 28 (no driver),
and picotool on Windows reported the board "appears to have a USB
serial connection, but picotool was unable" to open it. The Pico SDK
solves this with Microsoft OS 2.0 descriptors, which tell Windows 8.1+
to bind its inbox WinUSB driver to one interface of a composite device
with no INF: the device reports bcdUSB 0x0210, carries a BOS descriptor
with the MS OS 2.0 platform capability (UUID D8DD60DF-4589-4CC7-9CD2-
659D9E648A9F, Windows version 0x06030000, the set length, a vendor
code), and answers a vendor request (bmRequestType 0xC0, bRequest =
the vendor code, wIndex 7) with the descriptor set: a set header, a
function subset for interface 2, the compatible ID "WINUSB", and the
registry property DeviceInterfaceGUID = {bc7398c1-73cd-4cb7-98b8-
913a8fca7bf6}. Sources: Microsoft's "Microsoft OS 2.0 Descriptors
Specification" and the SDK's pico/usb_reset_tusb.h
(RPI_RESET_MS_OS_20_DESCRIPTOR) and pico_usb_reset/usb_reset.c
(desc_bos, desc_ms_os_20, tud_vendor_control_xfer_cb), tag 2.3.1.

sys/arch/rp2040/dev/usb.c now carries the same bytes: bcdUSB 0x0210,
DESC_BOS answered from usb_bos_desc, the vendor request answered from
usb_ms_os_20_desc. bcdDevice moved from 1.0 to 1.1 because Windows
caches what a device said about OS descriptors under
HKLM\SYSTEM\CurrentControlSet\Control\usbflags\<VID><PID><bcdDevice>,
so a host that had seen release 1.0 asks again for 1.1. The CDC
interfaces are not named in the set and keep usbser.

On this host after the kernel flash: Device Manager lists "Reset" as
OK, class USBDevice (WinUSB). `picotool reboot -u -f` from the
picotool-2.3.1-x64-win.zip build (raspberrypi/pico-sdk-tools
v2.3.1-0) returned the board to BOOTSEL in two seconds. The ROM's own
PICOBOOT interface ("RP2 Boot", PID 0003) still has no driver on
Windows; that is the ROM's descriptor set, not this kernel's, and the
mass-storage path does not need it.

## The flash sequence used

The kernel running before this round offered no driverless route into
BOOTSEL from Windows (no RB_BOOTLOADER, no WinUSB, no 1200-baud
trick), so the first entry was BOOTSEL held through a replug. From
there a script (this session's flash-board.sh, kept out of the tree)
copied unix.uf2 to RPI-RP2, waited for COM3, confirmed the WinUSB
binding, ran `picotool reboot -u -f`, copied flash.uf2, and waited for
COM3 again: 2 minutes 43 seconds end to end. Every later flash needs
no hand on the board: `bootloader` from the console or `picotool
reboot -u -f` from the host, then a file copy.

## 5. The web console held a session for a page that had left

Running the console the packaged way surfaced one more defect. A page
that reached the console through the short link was redirected to the
LAN address, opened its socket, and was navigated away from; Chrome
kept the socket open, and every later viewer saw "The console is in use
by another session" until the server was restarted. discobsd-web read
the viewer's frames with no bound, so any viewer that never closed (a
tab kept in the back-forward cache, a device that left the LAN) held
the one console forever.

Fix, PR #83, discobsd-host 1.0.7: the session loop reads through a
select()-based reader (the request handler's socket file refuses every
read after one timeout, a CPython rule, so it could not be reused with
a timeout), sends a WebSocket ping after 15 seconds of silence, and
ends the session after another 15 silent seconds; a pong or any frame
resets the count, a client ping is answered, and the serial reader and
the control replies share a lock so frames never interleave. Bytes the
handler read ahead with the handshake are carried into the reader. The
page closes its socket on pagehide and reloads on a pageshow from the
cache. Five tests cover it. On this host: down, up, and the next viewer
was in.

## Running the console from the port checkout on Windows

    cd ..\discobsd-pico-unofficial\distrib\rp2040\host
    python -m pip install --user .

    discobsd-console up        # web console and short link, detached
    discobsd-console status    # running or stopped, the URLs
    discobsd-console down      # stop both

`up` prints a short URL (port 42069) that redirects to the tokenized
console URL (port 7681) on the machine's LAN address, and creates
%APPDATA%\discobsd\web.env with the token, plus web.log, link.log and
the pid files. Open the full URL, or the short one from another device
on the LAN; on the machine itself http://127.0.0.1:7681/?token=... also
works. `discobsd-term` attaches the current terminal instead, with
Ctrl-] q to leave. Every command works the same from the release zip's
.exe files with no Python installed.

## Build environment

This host has no C compiler, bmake or ARM toolchain, so the port was
built in WSL Ubuntu 24.04 with the packages firmware.yml installs plus
gcc-multilib for the shell harness, and picotool 2.3.1 built from
source the way the workflow does. The build log carried zero
"warning:" lines. The CI run on PR #82 (rp2040 kernel, flash image and
gates) passed as well.
