# lexeme: behavior specification

`lexeme` is a clean-room word processor for DiscoBSD that follows the command
model of Microsoft Word 5.1 for UNIX and adds modern editing modes. This
document states the behavior the program implements, in this project's own
words. Its sources are short interface facts (menu names, key bindings,
prompt field names) from the original's menus, key list and manual, and the
screens the original drew when it ran on a Linux host under ibcs-us, and
the document files the original saves (see Word document files). No code,
table or prose of the original is reproduced here or in the program, and
features whose behavior those sources do not fix are left out
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
  A is local to that Replace; the form's confirm choice is unchanged. A
  Replace is one command: one Undo reverses all its replacements and one
  Redo makes them again. A replacement that cannot be stored ends the
  Replace, returns the document to its state before the Replace and keeps
  the failure message.
- Search text: `?` matches any one character, `^w` a run of spaces and
  tabs, `^t` a tab, `^s` a space, `^-` a hyphen, `^p` and `^n` a line
  break. `^^` for a caret and `^?` for a question mark are this program's
  additions.
- Undo reverses the last command whole; repeated Undo goes further back.
  The history holds 192 records. A delete, a paste, a formatting change,
  typing over a selection or a Replace writes at most three, however much
  text or how many formats it spans, and a full history drops its oldest
  command whole, so Undo never restores part of a command. A run of
  typing writes one record per key; when one run fills the history, its
  oldest keys are dropped and Undo steps back key by key to the oldest
  kept. An Undo that cannot restore a command for want of piece table
  room changes nothing and says so.
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
  Deleting a line break joins the text before it into the following
  paragraph, which keeps its own properties.
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
  properties of the text it holds. Typing over a selection replaces it in
  one step and one undo record; the new text takes the typing format when
  one is pending, otherwise the properties of the first replaced
  character. A replacement that cannot be stored leaves the selected text
  and its properties as they were.
- A formatting change, a paste and a delete are whole or not at all: the
  new property records, the room in the piece table and the stored text
  are all secured before the document changes. A change that needs more
  distinct formats than the tables hold (64 character, 32 paragraph
  records) is refused with the document and the tables as they were.
- The console shows bold as bold, underline and double underline as
  underline, italic in green, strikethrough in red, super- and subscript
  in magenta, hidden text in gray, uppercase and small capitals as
  capitals. Indents and centered or right alignment place the rows;
  justified paragraphs show left-aligned, and line spacing, space before
  and after and the keep options are kept but not drawn.
- A text file carries no properties. Transfer Save has a format field,
  Word or Text-only; it starts at Word for a document loaded from a Word
  file or named `.doc` or `.dcx`, and at Text-only otherwise. Saving a
  formatted document as Text-only asks first.

## Word document files

The program reads and writes the document files of Word 5.1 for UNIX.

Provenance. Every field, value and default the program uses is listed in
the table below with the file observation that establishes it. Two kinds of
observation were made, both of whole files: a document saved by the
original with one property set (bold, a centered paragraph, an indent) is
compared byte by byte with the same text saved plain, the baseline; and a
hand-built file with one field set to a chosen value is loaded into the
original to see whether it accepts the file and what it shows. No value
below rests on reading the original's code. The constants in
`lexeme_doc.c` each name their row.

The baseline is the text `ab` and one paragraph mark saved as a Word
document with nothing formatted. Offsets are bytes from the start of the
file or of a page; multi-byte values are little-endian.

| Row | Field | Value | Observation |
|---|---|---|---|
| W1 | page size | 128 bytes | every saved file is a whole number of 128-byte pages, and every page number in its header times 128 is the offset of a table the other rows describe |
| W2 | header 0x00 | 0xBE31 | present in every saved file; a hand-built file with another value is refused with "Not a Word document" |
| W3 | header 0x02, document type | 0 | 0 in every saved document; the shipped glossary and style sheet files carry 1 and 2; a hand-built document with 1 is refused |
| W4 | header 0x04 | 0xAB00 | present in every saved file; a hand-built file with 0xAB01 loads as plain text, header bytes included |
| W5 | text start; header 0x0E, text end | 0x80; 0x80 plus the text length | the baseline's text bytes start at 0x80 and 0x0E holds 0x84 (two characters and CR LF); each further typed character adds one to both the text and 0x0E |
| W6 | run pages; header 0x12 | character-run page at the first page after the text, paragraph-run page number at 0x12 | in the baseline the text ends in page 1, page 2 holds the character runs and 0x12 holds 3, the page of the paragraph runs |
| W7 | header 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x76, 0x78, 0x7C | the page count | in every saved plain document all eight hold one page number, the page after the paragraph runs; the original loads a file this program wrote with all eight set to its page count, showing its text and an italic word as italic in Format Character |
| W8 | header 0x6A, page count | file length / 128 | equals the page count of every saved file; a hand-built file whose count exceeds its length is refused |
| W9 | header 0x70 | 720 | every document saved with the default tab width of 0.5 inch carries 720 (twips) |
| W10 | header 0x7E, character set | 0x229B | present in every saved file; a hand-built file with 0 loads as an empty document |
| W11 | every other header byte | 0 | saved files carry a style sheet name at 0x1E, a printer name at 0x62, 0x0040 at 0x6C and 0x0105 at 0x74; the original loads a file this program wrote with all of these zero, showing its text and an italic word as italic in Format Character |
| W12 | paragraph mark | CR LF (0x0D 0x0A) | a document saved after typing Enter holds 0D 0A where Enter was typed |
| W13 | run page layout | first-run file offset (4 bytes) at 0; per run, its end offset (4) and a record pointer (2) from 4; pointer 0xFFFF for default properties, otherwise the record sits at the pointer plus 4; run count in byte 127; records packed down from byte 126 | the baseline's character page holds 0x80, one run ending at 0x84 with 0xFFFF, and count 1; a document with "ab" bold holds pointer 0x78 and the record at 0x7C; hand-built pages laid out this way load with their bold and centered runs |
| W14 | paragraph run entries | six more bytes per run after the run entries, zero | zero in every saved document that was not repaginated; hand-built pages with zeros load |
| W15 | run ends | the last character run ends at the text end, the last paragraph run one byte past it | in the baseline the character run ends at 0x84 and the paragraph run at 0x85 |
| W16 | property record | a length byte, then that many leading bytes of the property structure; trailing bytes equal to the default are left out | records saved by the original differ in length by property: 2 bytes for bold, 4 for underline, 6 for superscript, 15 for space after |
| W17 | character byte 0; byte 1 | 0; bold 0x01, italic 0x02 | the bold, italic and bold-italic documents differ from the baseline only in a record whose byte 0 is 0 and byte 1 is 0x01, 0x02 and 0x03 |
| W18 | character byte 2, size | half points, 24 by default | records that reach byte 2 for 12-point text hold 24; a hand-built record with 20 shows "font size: 10" in Format Character |
| W19 | character byte 3 | underline 0x01, strikethrough 0x02, double underline 0x04, uppercase 0x10, small capitals 0x30, hidden 0x80 | one saved document per property differs from the baseline in a record whose byte 3 holds that value |
| W20 | character bytes 4 and 5 | byte 4 zero; byte 5 superscript 0x0C, subscript 0xF4 | the superscript and subscript documents differ in a 6-byte record ending 00 0C and 00 F4 |
| W21 | paragraph bytes 0 to 3 | 0x3C, flags, 0x1E, 0 | every saved record of a body-text paragraph starts 0x3C; byte 1 is 0x01 centered, 0x02 right, 0x03 justified, 0x04 keep together, 0x08 keep with next, 0x10 side by side, one document each; every saved record reaching bytes 2 and 3 holds 0x1E and 0 there |
| W22 | paragraph 16-bit fields | right indent at 4, left at 6, first line at 8, line spacing at 10, space before at 12, space after at 14, in twips; line spacing 240 by default | a 1-inch right indent saves 1440 at 4; a 0.5-inch left indent 720 at 6; a 0.5-inch first-line indent 720 at 8 and a hanging indent -720 there; double spacing 480 at 10; one line before 240 at 12; one line after 240 at 14; records for single-spaced paragraphs that reach byte 10 hold 240 there |

This program's own choices, not claims about the format: a lone CR also
ends a paragraph; small capitals are read from 0x30 in character byte 3
and uppercase from 0x10 alone; a byte 5 other than 0x0C or 0xF4 reads as
superscript when positive and subscript when negative; a Word file with
another character set, or one shorter than its page count, is refused; a
Word file with more distinct character or paragraph formats than the
tables hold (64 and 32 records), or more runs than the piece table holds,
is refused before the document in the editor is touched;
fonts, colors, tab stops, styles, divisions, footnotes, bookmarks and the
summary are not kept, and a save writes the header, the text and the two
run tables.

## Modern modes

The key mode is chosen with `-k word`, `-k vi` or `-k nano`, the
`WORDKEYS` environment variable, or the Options form.

- vi mode follows the commands of this system's `vi` (stevie): h j k l,
  0 ^ $ w b e, [count]G and gg, Ctrl-D Ctrl-U Ctrl-F Ctrl-B, x X dd dw D C
  cw cc r yy p P J, i a I A o O, u, / ? n N, :w :q :q! :wq :x ZZ, :e file,
  :e!, :f, :N for a line number, and Ctrl-G. It adds Ctrl-R (redo), v
  (character selection, then d y c or x), :set wrap and :set nowrap,
  :menu (the Word COMMAND menu) and :help. A count before a command applies
  to x, X, J (joins that many lines), p, P and the motions; a count before
  an operator multiplies with one after it (2d3w removes six words, 2dd
  and d2d two lines). D, C, Y and r take no count.
- nano mode inserts every printable key and binds Ctrl-G help, Ctrl-X exit,
  Ctrl-O write, Ctrl-R insert a file, Ctrl-W search, Ctrl-\\ replace,
  Ctrl-K cut the line or selection, Ctrl-U paste, Ctrl-^ set the mark,
  Ctrl-C position, Ctrl-_ go to line, Ctrl-T the file picker, Ctrl-A and Ctrl-E line ends, Ctrl-Y and
  Ctrl-V page up and down, Ctrl-Z or Esc u undo, Esc e redo. Consecutive
  Ctrl-K cuts accumulate. Typing with the mark set replaces the marked
  text.
- A document reads as modified unless its text is the text last loaded or
  saved; undoing an edit back to that text reads as saved.
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
  target, so a failed save leaves the old file whole. The temporary is
  created exclusively under a name that carries the process id, so an
  existing file or link of any name is never written through; a Word
  save and a text save use the same temporary.
- SIGHUP, SIGINT, SIGQUIT and SIGTERM restore the terminal before the
  program ends by the signal.
- The document may exceed the process's memory: the loaded file stays on
  disk and is read on demand, and inserted or copied text spills to an
  unlinked temporary file when its memory buffer fills.

## Left out

The original's fonts, colors, tab stops, style sheets, Gallery,
windows, printing, spelling, thesaurus, hyphenation, outlining, macros,
glossaries, footnotes, bookmarks, annotations, revision marks and the
mouse: the sources give their names but not
behavior this program could reproduce in a plain-text document, or they
need a layout engine beyond the 192 KiB process window. Menu leaves for
them report that they are not available.
