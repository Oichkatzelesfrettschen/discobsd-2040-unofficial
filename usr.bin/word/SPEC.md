# word: behavior specification

`word` is a clean-room word processor for DiscoBSD that follows the command
model of Microsoft Word 5.1 for UNIX and adds modern editing modes. This
document states the behavior the program implements, in this project's own
words. Its sources are short interface facts (menu names, key bindings,
prompt field names) from the original's menus, key list and manual, and the
screens the original drew when it ran on a Linux host under ibcs-us.
No code, table or prose of the original is reproduced here or in the
program, and features whose behavior those sources do not fix are left out
and listed at the end.

## Screen

- The document fills the top of the screen. Below it, in the Word key mode,
  the command area takes one or more lines (the COMMAND menu wraps to the
  terminal width; three lines at 53 columns), then a message line, then the
  status line.
- The status line shows the page, line and column of the cursor
  (`Pg1 Li1 Co1`), the first characters of the scrap in braces (`{}` when
  the scrap is empty), the mode, and the file name with `*` when the
  document has unsaved edits.
- In the Word key mode a diamond end mark follows the last character of
  the document; the vi mode marks rows past the end with `~`.
- Paragraphs are the text between line breaks. With wrap on (the default)
  each paragraph wraps at the last space that fits the screen width, and a
  word wider than the screen breaks at the width. With wrap off a line runs
  past the right edge and the view scrolls sideways to keep the cursor
  visible.
- A page is 54 lines, which is what a 9-inch text area at six lines per
  inch holds; the program has no print layout.

## Command menu (Word key mode)

- Esc opens the COMMAND menu: Copy Delete Format Gallery Help Insert Jump
  Library Options Print Quit Replace Search Transfer Undo Window. Menus,
  forms and messages are in English.
- In a menu the command's capitalized letter runs it; Space, Tab and the
  arrow keys move the highlight and Enter runs the highlighted command. Esc
  leaves the menu and returns to the document.
- Submenus: Transfer (Load Save Clear Delete Merge Options Rename), Jump
  (Page), Library (Count Document-retrieval Run), Help, and the remaining
  submenus of the original's tree, whose leaves report that they are not
  available.
- A command that needs arguments shows a form on the command lines: the
  command name in capitals, then fields such as `filename:` or `text:`.
  A choice field lists its values with the current one in parentheses,
  `direction: Up(Down)`. Tab and Shift+Tab (or Down and Up) move between fields, Space or a
  value's first letter changes a choice, Enter carries out the command,
  Esc cancels it. F1 in a file name field opens the file picker.
- Transfer Load replaces the document with a file; Transfer Save writes
  the document; Transfer Clear empties it (confirming when it has unsaved
  edits); Transfer Delete removes a file; Transfer Merge inserts a file at
  the cursor; Transfer Rename renames the document's file; Transfer
  Options changes the working directory.
- Search finds text down or up from the cursor and selects it; Replace
  replaces each match, asking Y (replace), N (skip), A (all the rest)
  or Esc (stop) when confirm is Yes, and reports the number of replacements.
- Search text: `?` matches any one character, `^w` a run of spaces and
  tabs, `^t` a tab, `^s` a space, `^-` a hyphen, `^p` and `^n` a line
  break. `^^` for a caret and `^?` for a question mark are this program's
  additions.
- Undo reverses the last edit; repeated Undo goes further back.
- Jump Page moves to the start of a page; Library Run runs a shell command
  and waits for Enter; Library Count reports words, lines and characters; Library Document-retrieval opens the file picker;
  Options opens the options form; Quit leaves, asking Y (save), N (discard)
  or Esc (stay) when there are unsaved edits.

## Keys (Word key mode)

- Arrows move; Home and End go to the line ends; PgUp and PgDn move a
  screen; Ctrl+Left and Ctrl+Right move by word; Ctrl+Up and Ctrl+Down by
  paragraph; Ctrl+Home and Ctrl+End to the top and bottom of the window;
  Ctrl+PgUp and Ctrl+PgDn to the start and end of the document.
- For terminals without those keys, Ctrl-T is Home, Ctrl-E End, Ctrl-U PgUp,
  Ctrl-D PgDn; Ctrl-Z makes the next key shifted, Ctrl-A the next key an Alt
  key and Ctrl-C the next key a Ctrl key.
- Selection: Shift with a movement key extends the selection; F6 turns
  extend mode on and off; F7 and F8 select the previous and next word; F9
  and F10 the previous and next paragraph; Shift+F9 the line; Shift+F10 the
  whole document.
- Del (or Ctrl-X) deletes the selection to the scrap; Shift+Del deletes it
  without touching the scrap; Ins (or Ctrl-V) inserts the scrap; Alt+F3
  copies the selection to the scrap. Typing replaces a selection.
  Backspace deletes the character before the cursor.
- F4 and Shift+F4 repeat the last search; F5 toggles overtype; Shift+F1
  undoes; Ctrl+F10 saves.

## Formatting

- Every character carries character properties: bold, italic,
  underline, double underline, strikethrough, uppercase, small capitals,
  hidden, a position (normal, superscript, subscript) and a font size.
  Every paragraph carries paragraph properties: alignment (left,
  centered, right, justified), left, first-line and right indents, line
  spacing, space before and after, keep together and keep with next.
  A paragraph's properties are those of the line break that ends it.
- Format Character and Format Paragraph show those fields as forms;
  their values start from the selection's first character or paragraph,
  and Enter applies every field to the selection. Measurements take a
  number and a unit: `"` or `in`, `cm`, `p10`, `p12`, `pt` or `li`
  (a line, 12 points); a bare number is inches for indents and lines for
  spacing. A measurement that does not parse is refused.
- Speed keys (Alt, or Ctrl-A then the key): B bold, I italic, U underline,
  D double underline, K small capitals, S strikethrough, E hidden, + or =
  superscript, - subscript, Space normal characters; L, C, R, J alignment,
  F first-line indent of 0.5", N and M add or remove 0.5" of left indent,
  Q 0.5" on both sides, T a hanging indent, O one line before, 2 double
  spacing, P a normal paragraph.
- With no selection a character change applies to the text typed next,
  until the cursor moves; a paragraph change applies to the cursor's
  paragraph. Typed text takes the properties of the character before it.
  Undo and redo cover formatting changes, and the scrap keeps the
  properties of the text it holds.
- The console shows bold as bold, underline and double underline as
  underline, italic in green, strikethrough in red, super- and subscript
  in magenta, hidden text in gray, uppercase and small capitals as
  capitals. Indents and centered or right alignment place the rows;
  justified paragraphs show left-aligned, and line spacing, space before
  and after and the keep options are kept but not drawn.
- A text file carries no properties: Transfer Save writes the characters
  only.

## Modern modes

The key mode is chosen with `-k word`, `-k vi` or `-k nano`, the
`WORDKEYS` environment variable, or the Options form.

- vi mode follows the commands of this system's `vi` (stevie): h j k l,
  0 ^ $ w b e, [count]G and gg, Ctrl-D Ctrl-U Ctrl-F Ctrl-B, x X dd dw D C
  cw cc r yy p P J, i a I A o O, u, / ? n N, :w :q :q! :wq :x ZZ, :e file,
  :e!, :f, :N for a line number, and Ctrl-G. It adds Ctrl-R (redo), v
  (character selection, then d y c or x), :set wrap and :set nowrap,
  :menu (the Word COMMAND menu) and :help.
- nano mode inserts every printable key and binds Ctrl-G help, Ctrl-X exit,
  Ctrl-O write, Ctrl-R insert a file, Ctrl-W search, Ctrl-\\ replace,
  Ctrl-K cut the line or selection, Ctrl-U paste, Ctrl-^ set the mark,
  Ctrl-C position, Ctrl-_ go to line, Ctrl-T the file picker, Ctrl-A and Ctrl-E line ends, Ctrl-Y and
  Ctrl-V page up and down, Ctrl-Z or Esc u undo, Esc e redo. Consecutive
  Ctrl-K cuts accumulate.
- Key hints: with hints on (the default in nano mode, an option elsewhere)
  two lines above the status line name the main keys of the current mode.
- Redo (Ctrl-Y in Word mode, Ctrl-R in vi, Esc e in nano) reapplies an
  undone edit.
- File picker: lists the directory with subdirectories first, each marked
  `/`; arrows or j and k move, Enter opens a file or enters a directory,
  Backspace goes to the parent, a letter jumps to the next entry starting
  with it, Esc cancels.
- Options form fields: keys (Word vi nano), wrap (Yes No), hints (Yes No),
  color (Yes No), tab width (4 8). The command line takes `-k` for the
  keys, `-W` for wrap off and `-c` for no color.
- Color: the status line, key hints, selection, titles, errors and the end
  mark carry ECMA-48 colors; with color off they use reverse video and
  bold alone. The end mark uses the DEC line-drawing set.
- From later Word versions: redo, Library Count, a backup copy on save
  (the Transfer Save `backup` field keeps the old file as `name~`),
  accumulating cuts in nano mode, and typing that replaces the selection.
- vi and nano show the COMMAND menu and the forms over the last rows of
  the document.

## Files

- A document is bytes; line breaks are LF. Load and save keep every byte,
  including a missing final line break. Bytes outside printable ASCII show
  as `?` and are kept.
- Save writes a temporary file beside the target and renames it over the
  target, so a failed save leaves the old file whole.
- The document may exceed the process's memory: the loaded file stays on
  disk and is read on demand, and inserted or copied text spills to an
  unlinked temporary file when its memory buffer fills.

## Left out

The original's character and paragraph formatting, style sheets, Gallery,
windows, printing, spelling, thesaurus, hyphenation, outlining, macros,
glossaries, footnotes, bookmarks, annotations, revision marks, the mouse,
and its own document file format: the sources give their names but not
behavior this program could reproduce in a plain-text document, or they
need a layout engine beyond the 192 KiB process window. Menu leaves for
them report that they are not available.
