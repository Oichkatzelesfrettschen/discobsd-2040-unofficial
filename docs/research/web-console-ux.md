# The web console as a first-time user meets it

The DiscoBSD web console (`discobsd-web`, xterm.js 5.5.0 in a page the
server generates) was driven from headless Firefox 155 with Selenium
4.44 and geckodriver 0.37 against the live board, through the whole
journey a tester takes: load, log in, `ls`, the key bar, the keys help,
boot V6, `who`, Sync & leave. Screenshots and the DOM were captured at
each stage. This note records what the page does today, measures it
against the primary sources below, and lists the changes that follow.

Sources read for this note:

- xterm.js, "Design Document: Screen Reader Mode"
  (<https://github.com/xtermjs/xterm.js/wiki/Design-Document:-Screen-Reader-Mode>):
  an accessibility tree beside the rendered rows as a virtual list with
  `aria-posinset`/`aria-setsize`, an assertive live region for process
  output capped at 20 rows and cleared when the user types, a queue
  that tells typed characters from output so echoes are not read twice.
- xterm.js `ITerminalOptions`
  (<https://xtermjs.org/docs/api/terminal/interfaces/iterminaloptions/>):
  `screenReaderMode` (default false, "expose supporting elements in the
  DOM to support NVDA on Windows and VoiceOver on macOS"),
  `minimumContrastRatio` (default 1; 4.5 for WCAG AA, 7 for AAA, 21 for
  maximum).
- xterm.js issue 4269
  (<https://github.com/xtermjs/xterm.js/issues/4269>): with
  `screenReaderMode` on, Ctrl-P and Ctrl-F reached the browser's print
  and find dialogs; fixed by canceling key events regardless of the
  mode, and `attachCustomKeyEventHandler()` is the integrator's hook.
- VS Code, "Accessibility"
  (<https://code.visualstudio.com/docs/editor/accessibility>): a
  terminal accessible view (Alt+F2) that puts the whole buffer into the
  screen reader's browse mode, a minimum contrast ratio setting, an
  accessibility help command (Alt+F1), audio cues for state changes.
- WCAG 2.2 quick reference
  (<https://www.w3.org/WAI/WCAG22/quickref/>): 2.4.7 Focus Visible (AA),
  2.4.11 Focus Not Obscured (AA), 2.5.8 Target Size Minimum 24x24 CSS
  px (AA), 1.4.3 Contrast Minimum 4.5:1 (AA), 4.1.3 Status Messages
  (AA), 2.1.1 Keyboard (A), 2.1.4 Character Key Shortcuts (A).
- Bash manual, "Controlling the Prompt"
  (<https://www.gnu.org/software/bash/manual/html_node/Controlling-the-Prompt.html>):
  `\u` user, `\h` hostname to the first dot, `\w` `$PWD` with `$HOME` as
  `~`, `\$` `#` for uid 0 else `$`, `\[ \]` around non-printing text.
- fish shell, "Design" (<https://fishshell.com/docs/current/design.html>):
  discoverability ("a program should be designed to make its features
  as easy as possible to discover for the user"), configurability as a
  symptom ("a place where the program is too stupid to figure out for
  itself what the user really wants"), orthogonality.

## What the page does today, measured

| Property | Observed (headless Firefox, 1024x700) | Criterion |
| --- | --- | --- |
| Focus on load | the xterm textarea, `aria-label="Terminal input"` | 2.1.1 met |
| Focus after a key-bar click | returns to the textarea | met |
| Document language | `lang` unset | 3.1.1 Language of Page (A) not met |
| Button focus ring | `outline: none` | 2.4.7 not met: Tab through the bar shows nothing |
| Button targets | 32 to 142 px wide, 33 px tall | 2.5.8 met |
| Button text on button | #ddd on #333, about 10:1 | 1.4.3 met |
| Group labels | #888 on #111, about 5.3:1, 12 px | 1.4.3 met, small |
| Status line | #6a6 on black, 12 px, no live region | 4.1.3 not met: "connected", "session closed" are silent to a screen reader |
| Ctrl one-shot | a CSS class only | state invisible to assistive tech; needs `aria-pressed` |
| `hide` | removes the key bar with no way back | a trap on a phone: no keys, no Ctrl, no way to leave V6 |
| `screenReaderMode` | off | the accessibility tree and live region xterm.js offers are never built |
| `minimumContrastRatio` | 1 | V6's and `ls`'s colors go to the screen as is; bold blue on black is about 4.6:1, plain blue about 2.4:1 |
| `keys` help | a `div` toggled by a button, no role, not announced | needs `role=region`, an accessible name, and announcement |
| Character shortcuts | none outside the terminal | 2.1.4 met |
| `ls` colors | directory cells arrive as fg 4 bold: rendered | works |
| V6 in the page | `#` prompt, `who` answers `tty8` | works; Backspace sends DEL, which V6 reads as interrupt |
| Sync & leave | narrates sync, leaving V6, sync, logging out, session closed; ends at `login:`; socket closed | works, narration is plain text in the terminal and status |

## What a first-time user hits

1. **Nothing says which system they are in.** The DiscoBSD prompt now
   says `operator:~$` and V6 says `#`; the page could say it too, in the
   status line, from the bytes it sees (the `pdp11:` banner and the
   `[pdp11: exit` line bracket a V6 session).
2. **`root:/home/operator#` after `su` reads as "root of
   /home/operator".** The bash convention `user@host:dir` disambiguates
   because `@host` separates who from where; `su` keeps the directory
   on Linux the same way. The port adopts `\u@\h:\w\$`, which gives
   `operator@pico:~$` and `root@pico:/home/operator#`.
3. **Backspace interrupts in V6.** Documented; the key bar's `# erase`
   is the way to erase.
4. **The key bar can be hidden and not recovered.** `hide` becomes a
   toggle that leaves a one-button tab.
5. **A screen reader user gets no announcements.** The status line and
   the leave narration need `aria-live`; xterm.js's screen reader mode
   needs a switch the user can find.
6. **Keyboard users cannot see where Tab went** in the key bar: no focus
   ring.
7. **Ctrl one-shot state is a color only.**
8. **The V6 clock says 1970**; no fix without a host time source (V6
   `date` takes `MMDDhhmm[yy]`, 2026 is out of its range).

## The changes that follow

- `bin/sh`: `\h` joins `\u \w \$`; profiles ship `\u@\h:\w\$ `.
- `discobsd-web`: `lang="en"`; every button has an `aria-label`; the
  Ctrl toggle carries `aria-pressed`; buttons show a focus ring; the
  status line is `role="status" aria-live="polite"` and the leave
  narration goes through it; `minimumContrastRatio` 4.5; a `reader`
  toggle turns `screenReaderMode` on (xterm.js 5.5.0 carries the 4269
  fix) and is remembered in `localStorage`; `hide` becomes a toggle
  with a `keys` tab left on screen; the keys help is a labeled region.
- The port README's console sections say what the status line reports
  and how to turn on the reader mode.

## What stays open

- Real screen reader runs (NVDA, VoiceOver, Orca) were not performed;
  the DOM and ARIA state were verified in headless Firefox only.
- V6 has no way to learn the date from the host.
- discobsd-term in a terminal program inherits that program's
  accessibility; the escape menu is the same on every OS.
