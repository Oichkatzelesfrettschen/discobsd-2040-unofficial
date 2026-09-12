# usr.bin/menu: a Rockbox-shaped menu shell, built from scratch

This implements the recommendation in `rockbox-ui.md`: a small, original,
BSD-licensed menu/list shell for the USB console, modeled on the *design*
of Rockbox's action-table and cursor-list widget and none of its code.
Source: `usr.bin/menu/menu.c` (one file), `usr.bin/menu/Makefile`,
`usr.bin/menu/test-pty.py`, sample config `etc/menu`.

## Design and what each part mirrors

**Action mapping (mirrors `apps/action.c`'s keymap-to-enum idea).**
`struct keymap[]` is a flat table of `{ key, action }` pairs; `key_to_action()`
looks a raw key code up once and returns one of five verbs: `ACT_UP`,
`ACT_DOWN`, `ACT_SELECT`, `ACT_BACK`, `ACT_QUIT`. Everything past that
point -- the main loop's `switch` -- deals only in `enum action`, never in
a raw key value, which is the actual idea worth copying from `action.c`:
one place decides what a key means, and the rest of the program is blind
to which key produced which action. `j`/`k` and the arrow keys both map to
`ACT_UP`/`ACT_DOWN`, proving the indirection does something (two different
key codes reach the same behavior through the same table entry point,
never through a duplicated `if` in the caller).

**List widget (mirrors `apps/gui/list.c`'s cursor/scroll/redraw split).**
`struct list` holds exactly what `list.c`'s widget holds and no more:
`count`, `cursor`, `top` (the scroll offset) and `visible` (the window
height), plus a `get_label(index, buf, buflen)` callback -- the widget
owns no item storage, `list.c`'s own split between "the list mechanism"
and "whatever the caller is listing." `list_move()` reproduces `list.c`'s
minimal-redraw idea directly: a cursor move that stays inside the visible
window redraws only the two rows that changed (the old cursor row plain,
the new one reversed); a move that would push the cursor outside the
window scrolls `top` and redraws the whole viewport. No Rockbox text is
read into this function -- the redraw strategy is inferred from
`rockbox-ui.md`'s description of `list.c`'s role, not from `list.c`'s
source, which this port never opens.

**What has no Rockbox analog.** Entry storage (`struct entry[64]`),
`/etc/menu` parsing, the three built-in prompted actions, and running a
command through `fork()`/`execlp("/bin/sh", ...)`/`wait()` are new code
with no Rockbox counterpart -- Rockbox's menu system builds submenus,
settings screens and WPS integration this program has no use for and
does not attempt.

**Terminal layer.** Follows `usr.bin/kilo/kilo.c` and `usr.bin/re/r.ttyio.c`
exactly: `TIOCGETP`/`TIOCSETP`/`TIOCGETC`/`TIOCSETC`/`TIOCGLTC`/`TIOCSLTC`
sgtty raw (`CBREAK`, `ECHO` off) on the target, real `<termios.h>` on the
host build, both gated on the same `TERMIOS` macro kilo.c tests. `read_key()`
is kilo's escape-sequence decode cut down to the four arrow keys (no
`DEL`/`HOME`/`END`/page keys, since the menu has no use for them), with the
same `FIONREAD` poll after a lone `ESC` that a CBREAK terminal's missing
`VMIN`/`VTIME` requires. Screen drawing uses the same three ANSI sequences
kilo uses: `\x1b[2J` (clear), `\x1b[r;cH` (cursor position), `\x1b[7m`
(reverse video) -- no libcurses, no terminal-size query, a fixed 80x24
layout as the port's console already is.

**Entries.** Three built-ins install first (`Run a program`, `Show a file
(pager)`, `List a directory`); each prompts on the status line for its one
argument (`prompt_line()`, a small raw-mode line editor with backspace and
ESC-to-cancel, since `ECHO` is off) and formats it into a fixed command
template (`"%s"`, `"more %s"`, `"ls -l %s"`). `/etc/menu` then appends
`"label: command"` lines -- blank lines and `#`-comments skipped, leading
and trailing blanks trimmed -- up to the 64-entry cap. Every command,
built-in or configured, runs as `sh -c COMMAND` so pipes and redirection
work the way they would from an interactive shell.

## Static allocation and size

No `malloc`, no floating point, one `struct entry entries[64]` array sized
at compile time (`LABEL_MAX` 32, `CMD_MAX` 80 bytes per entry). Measured
with `arm-none-eabi-size` after a `bmake MACHINE=rp2040` build:

```
   text	   data	    bss	    dec	    hex	filename
  10636	    420	   7524	  18580	   4894	menu.elf
```

The installed a.out is 11,088 bytes (text + data + the a.out header),
under the 16 KB budget `STORAGE.md` sets for an a.out's text+data, and well
under the 96 KB user-process window. bss is 7,524 bytes, almost entirely
the entry table (64 x roughly 116 bytes); this is the "few KB of static
buffers" the task budget calls for, not the tens of KB a `malloc`-based
item list would risk. Both the target build (`arm-none-eabi-gcc -Wall
-Wextra`) and the host build (`cc -Wall -Wextra -DTERMIOS`) compile with
zero warnings.

## Host test

`usr.bin/menu/test-pty.py`, run via `bmake test` (`${PYTHON:-python3}
test-pty.py ./menu.host`): forks the host build under a pty pointed at a
temporary `/etc/menu` with two entries, checks the header paints, sends
three Down presses to walk the cursor off the three built-ins and onto the
first configured entry ("Say hi: echo hi-from-menu"), confirms the cursor
landed there (its label appears reversed on screen), sends Enter, confirms
the command's own stdout ("hi-from-menu") reached the pty, dismisses the
"press any key" pause, sends `q`, and checks the process exited with
status 0. This exercises the keymap dispatch, the list widget's cursor
movement, `/etc/menu` parsing, and the `fork`/`execlp`/`wait` command path
end to end; the sgtty ioctls are the only piece this test cannot reach
(the host build compiles the `TERMIOS` half of `menu.c` instead).

## Build and manifest wiring

- `usr.bin/menu/{menu.c,Makefile,test-pty.py}`, added to `usr.bin/Makefile`'s
  `SUBDIR` next to `kilo` (unconditional, like `kilo` and `re`: it builds on
  every machine, and only `distrib/rp2040/mi.rp2040` ships it).
- `etc/menu`, a four-line sample config (`df`, `ps`, `date`, `uname -a`,
  all present on the rp2040 root per `distrib/rp2040/mi.rp2040`), installed
  by `etc/Makefile`'s new `MDFILES` variable, populated only when
  `MACHINE=rp2040` and installed into `${DESTDIR}/etc` alongside the
  existing `FILES` list.
- `distrib/rp2040/mi.rp2040` gained `file /etc/menu` (next to the other
  `/etc` file entries) and `file /usr/bin/menu` (next to `/usr/bin/kilo`).
- Verified in a worktree with a full `bmake MACHINE=rp2040 build`, then
  `bmake -C etc MACHINE=rp2040 DESTDIR=.../destdir.rp2040 distribution`,
  then `bmake MACHINE=rp2040 fs`: fsutil reports "Installed 18
  directories, 51 files, 19 devices, 64 links, 1 symlinks" and the
  manifest it built (`distrib/rp2040/_manifest`) carries both `file
  /etc/menu` and `file /usr/bin/menu`.

## Not done, and why

No submenus: `ACT_BACK` is wired into the action table (mirroring
`action.c`'s shape even where this program has only one menu level to pop
out of) but currently behaves like `ACT_QUIT` at the top level, documented
in a comment at the call site. Adding a second level would need a stack of
`struct list` plus a "menu item points at another menu" entry kind;
nothing in the current data structures forecloses it, but it is unbuilt
because the task's flat `/etc/menu` has no notion of a submenu to point
into yet.
