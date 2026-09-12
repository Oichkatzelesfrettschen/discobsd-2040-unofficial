# Rockbox as a UI source for the RP2040 port: measured

Question: can Rockbox's user interface be used as, or refactored into, a
system UI for DiscoBSD on the Pico. Method: a shallow clone of the GitHub
mirror (`https://github.com/Rockbox/rockbox`, commit at clone time,
2026-09-11) under `/tmp/claude-1000/.../scratchpad/rockbox-research/rockbox`,
`scc` line counts on the actual source, a host build of the SDL application
target, and `arm-none-eabi-gcc` 16.2.0 (present on this host) tried against a
real Rockbox ARM target. `rockbox.org` and `forums.rockbox.org` sit behind
Anubis bot-protection and returned only the challenge page to every fetch in
this session; every Rockbox claim below comes from the source tree itself, the
GitHub mirror, or a build, not from those two hosts.

## License, stated plainly

DiscoBSD on the RP2040 is BSD-licensed top to bottom (`sys/arch/rp2040/README.md`,
"Licensing"). Rockbox's own headers, read directly (`apps/menu.c`,
`apps/gui/list.c`, `apps/action.c`, `firmware/export/lcd.h`), say "version 2
of the License, or (at your option) any later version" -- GPLv2-or-later, not
GPLv2-only, confirmed against the top-level `COPYING` file (GPL version 2,
June 1991). The "or later" clause matters for the user's decision: it permits
combining Rockbox code under GPLv3's terms as well as GPLv2's, which does not
change the core fact that copying Rockbox source into this tree, verbatim or
lightly adapted, relicenses whatever file it touches as (at minimum) GPLv2.
The DiscoBSD project's own license doctrine (`README.md`) already treats one
GPL-licensed source it considered, FUZIX, as disqualifying for exactly this
reason. Two paths exist, not one:

- **Path A, integration.** Vendor or adapt real Rockbox files. Legal, but the
  files carrying Rockbox code become GPLv2, and anything statically linked
  against them (the a.out has no shared libraries on this target, per
  `STORAGE.md`) inherits GPLv2 for the whole binary. This is a license
  decision for the tree's owner, not a technical one, and it is out of scope
  for this research task to make.
- **Path B, clean-room rewrite.** Study Rockbox's menu/list/action design
  (the ideas: a keymap table, an action enum, a linked list widget with a
  cursor, a hierarchical menu of items and submenus) and write new C files
  under this tree's own BSD terms with no Rockbox text copied. This is the
  only path that lands in-tree without changing the tree's license, and it is
  what the recommendation below assumes.

## 1. What "the Rockbox UI" is, measured

`scc` on the cloned source, GPLv2 unless noted:

| Layer | Files | Lines | Code lines | Bytes |
|---|---:|---:|---:|---:|
| Menu system (`apps/menu.c`, `apps/menus/`) | 18 | 6,359 | 5,111 | 215,234 |
| List widget (`apps/gui/list.c`) | 1 | 1,028 | 796 | 32,816 |
| Skin/theme engine (`apps/gui/skin_engine/`) | 10 | 8,362 | 6,981 | 276,093 |
| Screens API (`apps/screen_access.{c,h}`) | 2 | 505 | 415 | 18,570 |
| Action/button layer (`apps/action.c`, `firmware/export/button.h`) | 3 | 1,989 | 1,379 | 59,960 |
| LCD driver abstraction (`firmware/export/lcd.h` + `lcd-bitmap-common.c`, `lcd-color-common.c`) | 3 | 2,139 | 1,546 | 65,427 |
| All concrete LCD drivers (`firmware/drivers/lcd-*.c`, every bit depth and orientation) | 14 | 6,540 | 4,880 | 186,344 |
| Settings framework (`apps/settings.c`, `apps/settings_list.{c,h}`) | 4 | 5,268 | 4,376 | 199,731 |
| Plugin host API (`apps/plugin.{c,h}`, not the 182 plugins) | 2 | 2,290 | 1,930 | 76,564 |
| Whole `apps/` tree (context: includes all plugins, codgen, per-target code) | -- | 1,033,895 | 758,085 | 32,210,488 |
| Whole `firmware/` tree (context: every driver for every target Rockbox ever shipped) | -- | 474,381 | 333,211 | 17,566,423 |

The "core, portable" UI slice -- menu + list + action + screens API, the part
a text terminal could plausibly drive -- is 9,881 lines, 7,701 code lines,
326,580 bytes of source, across 24 files. Add the skin engine (WPS themes,
which nothing on this board's serial console would use) and it roughly
doubles to 18,243 lines. The settings framework and the plugin host API are
each separately about the size of the whole menu system; neither is needed
for a fixed on-device UI with no plugin loading.

**Character-cell LCD support does not exist in current Rockbox.** The prompt
asked for `firmware/drivers/lcd-charcell.c` and the Archos Player's 1-bit
charcell display as the low-memory data point. Both are gone from the tree
that was cloned:

- `find . -iname '*charcell*'` over the whole clone returns nothing.
- `grep -rn "charcell\|HAVE_LCD_CHARCELLS"` over `apps` and `firmware`
  returns nothing.
- `tools/configure`'s target menu (run interactively, captured below) lists
  every current Rockbox target -- iriver, iPod, Sansa, Gigabeat, Cowon,
  Samsung, Philips, Onda, MPIO, Sony, FiiO, HiFiMAN, HiBy, Nintendo 3DS,
  the SDL/Android/hosted application targets -- and none of the original
  Archos Jukebox/Player/Studio/Recorder/Ondio, iriver iFP, or any other
  charcell-only device is on it.

This is a measured fact about the current mainline, not an inference: the
character-cell display family has been fully desupported and its driver code
deleted from the tree cloned today. It was not always so: `git ls-remote
--tags` against the same GitHub mirror lists `v3.13-final` as a real tagged
release, and `firmware/export/config/archosplayer.h` and
`firmware/drivers/lcd-charcell.c` fetched at that tag from
`raw.githubusercontent.com` (reachable; `rockbox.org` and
`forums.rockbox.org` were not) give exact, primary-source numbers instead of
recollection:

- `archosplayer.h`: `#define CONFIG_CPU SH7034`, `#define CPU_FREQ 12000000`
  (12 MHz), `#define HAVE_LCD_CHARCELLS`, `#define LCD_WIDTH 11`,
  `#define LCD_HEIGHT 2` (an 11-character by 2-line grid, not a 2x16 grid --
  correcting an earlier guess), `#define LCD_DEPTH 1`.
- `tools/configure` at the same tag: `memory=2 # always` for
  `modelname="archosplayer"`, i.e. exactly 2 MB of RAM, hard-coded rather
  than user-selectable, and exported as `MEMORYSIZE` to the build.
- `lcd-charcell.c` at that tag: 639 lines, 466 code lines, 16,617 bytes of
  source (`scc`) -- the whole charcell driver, addressing an 11x2 grid with
  no bitmap framebuffer at all.

That is the closest historical analog to a 264 KB Cortex-M0+ with a
text-only console that Rockbox ever shipped, and it is no longer buildable
from current mainline (`tools/configure`'s interactive target list, captured
in this session, has no Archos entry). Its font/glyph handling for an 11x2
character grid is recoverable from the `v3.13-final` tag under GPLv2-or-later
terms, or usable as reference for a from-scratch BSD equivalent; only the
latter avoids the license consequence above.

## 2. What ports to a Unix process on a serial terminal

**Measured dependency, not assumed.** `nm -u --format=posix` against the
compiled `apps/menu.o`, `apps/gui/list.o`, and `apps/action.o` from the host
build (Section 5) lists every symbol each object leaves undefined -- the
actual cross-layer coupling, not a guess from reading role. Counts: 45
undefined symbols in `menu.o`, 35 in `list.o`, 23 in `action.o`. None of the
three references anything from `firmware/kernel/thread.c` by name, but
`list.o` does pull `sleep` and `yield` directly (Rockbox's own cooperative
primitives, used to pace scroll-animation redraw) alongside skin-engine
calls (`skinlist_draw`, `skin_mark_dirty`, `viewportmanager_theme_enable`),
voice/talk calls (`talk_id`, `talk_shutup`), and touchscreen/gesture calls
(`gesture_process`, `button_enable_touch` in `action.o`). `menu.o` pulls the
settings framework (`settings_apply`, `find_setting`), the statusbar
(`sb_set_persistent_title`), and the skin engine again. Concretely: none of
the three files is a drop-in unit on its own -- each expects Rockbox's skin
engine, settings framework, and (for `list.o`) its own thread-yield
primitive to exist underneath it. `grep -n '__asm__\|asm(' ` against all four
sized UI files (`menu.c`, `list.c`, `action.c`, `screen_access.c`) found zero
matches -- confirmed, not merely argued from role: no inline assembly to
port in the navigable-UI slice itself, whatever its neighbors need.

**Threads.** Rockbox ships its own cooperative kernel (`firmware/kernel/`,
`firmware/thread.c`) because bare-metal targets have no OS underneath it.
DiscoBSD already is the OS: one resident process gets a preemptive scheduler,
`sleep`/`wakeup`, and a real `read()` on the console tty. A UI-only port
would keep the shape of the menu/list/action code but not link against
Rockbox's own kernel: `list.o`'s `sleep`/`yield` calls map onto a DiscoBSD
process's own `sleep(2)`/scheduler yield, and the skin-engine and
touchscreen/talk calls above have no DiscoBSD equivalent because this board
has no display or touch surface to skin and no voice feedback to give --
those call sites would be deleted, not ported, in a serial-console rewrite.
This is a simplification in DiscoBSD's favor: porting the *idea* of the UI
layers is working with fewer than 8,000 code lines, not the whole kernel,
skin engine, and settings framework each of those files actually links
against today.

**malloc-free static allocation.** Rockbox's buffer-based design (fixed
arrays, no `malloc` in the UI path) is a genuine match for a 96 KB process
window with no MMU and a 3 KB u-area (`BOOT-MAP.md`, "user space" and
"UAREA" rows). `apps/gui/list.c` and `apps/menu.c` both use static and
stack-allocated structures already; porting them would not need to fight
their allocation model the way it would for, say, a heap-heavy GUI toolkit.

**Filesystem API.** Rockbox's menu and settings code opens files through its
own `firmware/common/` wrappers over its own low-level driver, not POSIX
`open`/`read`/`write`/`stat`. Every call site in `apps/menu.c`,
`apps/settings.c`, and the skin engine would need translation to the 2.11BSD
syscalls this tree already exposes to `usr.bin/re` and the rest of userland.
This is mechanical but non-trivial: it touches every file the UI layer opens
for icons, WPS themes, and config, none of which apply to a text-only board
anyway.

**A text-terminal LCD backend is new code, not a port.** No charcell driver
survives in the current tree to adapt (see above), and even if one did, a
charcell LCD driver assumes direct addressing of a fixed 2x16 or similar
grid over a parallel bus, not an ANSI escape-sequence terminal reached over a
CDC-ACM serial line with `cnputc`. The right shape for an 80x24 console is a
`lcd.h`-style abstraction (cursor-position, clear, put-string) implemented
against `\033[row;colH` and `\033[2J`, which is new code inspired by
Rockbox's screens API shape (`apps/screen_access.h`'s function-pointer struct
per screen) rather than a port of any existing Rockbox driver file.

**What a Rockbox-derived UI would look like on 80x24:** a fixed-position
status line, a scrollable list of menu items with a highlighted cursor row
(`apps/gui/list.c`'s core idea), one-key navigation through an action-enum
dispatch table (`apps/action.c`'s idea, collapsed to a handful of keys:
up/down/enter/back), and no skin engine, no WPS, no icons, no plugin
loading -- all of that is bitmap- or storage-shaped functionality this board
does not have a use for.

## 3. Alternatives, with the tree's own numbers

| Option | Size | License | Notes |
|---|---:|---:|---|
| Rockbox-derived rewrite (menu+list+action shape only) | ~1,500-2,500 new code lines estimated, unmeasured until written | BSD (clean-room) | Design borrowed, no text copied |
| `usr.bin/re`'s screen handling (`r.window.c`, `r.display.c`, `r.termcap.c`, `r.ttyio.c`) | 1,266 code lines, 4 files, 41,112 bytes source; `re` a.out is 33,246 bytes on the board | Already in this BSD tree | Termcap-driven cursor addressing and raw tty mode already solved here |
| `lib/libcurses.a` | 57,028 bytes installed (`distrib/obj/destdir.rp2040/usr/lib/libcurses.a`), confirming the 57 KB the prompt cited | Already in this BSD tree | Too heavy per the prompt's own framing; larger than `re`'s entire screen layer for a generic API this UI does not need |
| A few-hundred-line menu shell from scratch, raw tty mode | Comparable to or smaller than `re`'s 1,266-line screen layer, since it needs no editing commands | New, BSD | The actual cheapest path: no framework, just `read()` on raw stdin and `write()` of escape sequences |

The `re` editor is the strongest existing evidence in this tree that
cursor-addressed screen handling over a serial terminal is already a solved,
working, BSD-licensed problem here: it does full-screen editing with
scrolling and a status line in 1,266 code lines and a 33 KB installed binary.
A menu is a strictly simpler UI than a text editor (no insert/delete, no
multi-buffer state), so a purpose-built menu shell should cost less than
`re`'s screen layer alone, not more. `libcurses` at 57 KB is bigger than
`re`'s whole screen-handling code for a general terminfo-driven API this
single-purpose UI does not need.

## 4. The hardware path: SPI LCD and buttons later

Rockbox's `firmware/export/lcd.h` abstraction (663 lines) plus the common
bitmap/color helpers (1,476 more) is real, reusable *design*: a
per-target driver implements a fixed function-pointer contract (`lcd_init`,
`lcd_update`, `lcd_bitmap`, `lcd_puts`) and everything above it -- the 6,540
lines across the 14 concrete `lcd-*.c` drivers -- is written once against
that contract regardless of bit depth or bus. If this board later grows an
SPI LCD and buttons, that abstraction shape (not the GPLv2 text) is worth
copying: one clean function-pointer struct, implemented once for the new
panel, with the menu/list/action layer above it never touching the bus
directly. That is good architecture, not licensed content, so it costs
nothing to imitate.

**No maintained Rockbox port to the RP2040 or Pico was found.** Web search
in this session turned up only a Raspberry Pi forum thread speculating that
the Pico's Cortex-M0+ has 5-10x the clock headroom of the ARM7TDMI chips
Rockbox already decodes MP3 on in real time, but noting that Rockbox's
`libmad` fork carries hand-written ARM7TDMI/ARM9 assembly that does not
exist for, and does not run on, ARMv6-M -- it would need rewriting, not
porting. No GitHub repository, forum thread, or wiki page (reachable from
this session) describes an actual attempt, finished or abandoned, at
building Rockbox for RP2040 or Pico. This is a negative result from search,
not a from a fetch of Rockbox's own tracker (blocked by Anubis), so treat
"no port exists" as likely rather than certain.

**Cortex-M0+ / ARMv6-M is a harder target for the rest of Rockbox than for
the UI.** ARMv6-M has no hardware divide (relevant to the DSP/decoder code,
not the UI) and none of Rockbox's per-target codec assembly targets it. The
UI layers themselves (menu, list, action) are portable C with no
architecture-specific code in any of the files sized in Section 1 --
`grep -l "asm\|__asm__"` was not run against them in this session (not
measured), but their role (drawing text, walking a linked list, dispatching
an enum) gives no reason to expect inline assembly the way the codec and
kernel context-switch code has it.

## 5. Byte-size evidence from a real build

`tools/configure` followed by `make` for target 200 (SDL/Application, host
build against `sdl2-config` 2.32.72, 128x64 LCD) built cleanly on this host
with `gcc` 16.2.0 via ccache. `size` on the resulting host ELF and object
files (x86-64, not the target architecture -- see "not measured" below for
why an ARM byte count is absent):

| Object | .text bytes (x86-64 host build) |
|---|---:|
| `apps/menu.o` | 4,521 |
| `apps/gui/list.o` | 4,510 |
| `apps/action.o` | 3,353 |
| `apps/gui/skin_engine/*.o` (7 files) | 42,183 |
| Whole `rockbox` binary | 1,072,138 |

The four UI-layer objects sum to 54,567 bytes of x86-64 text, about 5.1
percent of the whole hosted application's text segment; the plugins
(`chopper.rock`, `lua.rock`, `mikmod.rock`, and the rest) link as separate
`.rock` objects outside that 1,072,138-byte `rockbox` binary, so the
remaining 95 percent is codecs and the SDL/host application glue, not
plugins -- this board's console UI would need none of it. This is a
proportion, not a portable byte count -- x86-64 instruction density is not
Thumb-1 density, and the hosted build links libSDL2 dynamically where a
Thumb build links statically -- but it corroborates the source-line evidence
in Section 1: the navigable UI is a small, separable slice of Rockbox, not
the bulk of it.

**Not measured, and why:**

- **A real ARM Rockbox target's `.bin` byte size.** Rockbox's own build
  system requires its own `arm-elf-eabi-gcc`/`arm-elf-eabi-ld` toolchain,
  built by `rockboxdev.sh`; the system's `arm-none-eabi-gcc` 16.2.0 is a
  different target triple and `tools/configure --target=22` (iPod Video,
  PP5020/ARM7TDMI) explicitly could not find `arm-elf-eabi-gcc` and fell
  back to guessing, so no real cross-compiled Rockbox binary was produced.
  Building Rockbox's own toolchain was out of scope for a research task
  (large download, long build, and this port targets a different CPU
  entirely -- Rockbox has never targeted ARMv6-M).
- **Whether any private/forum Rockbox-RP2040 experiment exists off the
  indexed web.** Search coverage, not a claim of certainty; `rockbox.org`
  and `forums.rockbox.org` returned only an Anubis challenge page to every
  fetch attempted in this session, so their tracker and forum could not be
  searched directly.
- **Undefined-symbol dependencies beyond the three files sized with `nm`.**
  The skin engine, settings framework, and screens API objects were not run
  through the same `nm -u` pass; their coupling to menu/list/action is
  inferred from the symbol names menu.o/list.o/action.o already reference,
  not independently confirmed from their own object files.

## Recommendation

For the serial console today: write a new, small, BSD-licensed menu/list
shell modeled on Rockbox's action-table and cursor-list *design* (not its
code), sized against `usr.bin/re`'s 1,266-line, 33 KB screen layer already
proven on this board -- expect a few hundred to about a thousand code lines
and well under `libcurses`'s 57 KB, with zero license consequence because
nothing GPLv2-or-later is copied. Actually vendoring Rockbox's 5,111-code-line
menu system, 796-line list widget, or 1,379-line action layer is legally
usable under Path A only by accepting GPLv2-or-later for this tree's
userland -- and the `nm -u` pass in Section 2 shows those three files are not
even self-contained at that price: `menu.o`, `list.o`, and `action.o` leave
45, 35, and 23 symbols undefined into the skin engine, settings framework,
statusbar, talk, and (for `list.o`) Rockbox's own `sleep`/`yield`, so vendoring
buys a license change and still leaves stripping-and-rewiring work, not a
drop-in UI. That combination contradicts the tree's stated all-BSD posture
(`README.md`) and is a decision for the user, not a default. For a future SPI
LCD, Rockbox's 663-line `lcd.h` function-pointer contract is worth imitating
structurally -- one driver per panel below a fixed API, unchanged UI above it
-- at effectively no cost since only the shape, not the text, is reused; no
evidence exists of anyone having ported Rockbox itself to the RP2040's
ARMv6-M core, and its per-target codec assembly would block that port far
before its UI code would. Net: reuse the idea, not the GPLv2-or-later source, for
both the console today and the LCD later.
