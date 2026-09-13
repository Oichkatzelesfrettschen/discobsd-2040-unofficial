# bin/sh against the POSIX Shell Command Language

`bin/sh` is the 2.11BSD Bourne shell -- Bell Laboratories source through the
KIAE 8-bit adaptation, whose `@@@` comments mark every place a PDP-11
assumption about the sign or width of `char` was replaced -- plus this port's
`edit.c`, an interactive line editor with a 32-entry history ring and
PATH/filename tab completion that `main.c` engages only when both ends of the
top-level command line are a tty. This ledger records what that shell answers
against XCU chapter 2, section by section, with the source location that
implements each requirement or the harness case that exercises it.

## Method

Rows are numbered by the requirement id of the same clause in the XINIM
repository, so the two projects cross-reference: the section anchors
`tag_18_01` through `tag_18_14` of `docs/posix/posix_shell_language_ledger.tsv`
map one-to-one onto XCU 2.1 through 2.14, and the token-recognition rows
`tag_18_03.p01` through `tag_18_03_01.a03` of
`docs/posix/posix_token_recognition_requirements.tsv` carry the ten ordered
rules and the alias paragraphs. Those TSVs declare IEEE Std 1003.1-2017 and
Base Specifications Issue 7 as their authority. The `susv5.html` and
`posix-2024.html` files beside them are 682-byte and 684-byte frameset stubs
with no specification text, so no row cites a section from them; the
requirement text quoted here comes from the TSV assertion columns, and rows
outside the TSVs cite the XCU section number alone.

Evidence is one of two kinds. A source citation names the file and line that
decides the behavior. A test citation names a case in
`bin/sh/tests/posix-sh.sh`, which builds these same sources for the build host
and compares the exact bytes and exit status of 118 snippets. A case declared
with `xfail()` there carries the POSIX answer this shell does not give and
fails the moment the shell starts giving it, so a landed fix forces its row in
this ledger to move.

Status values: `pass` for a requirement met, `partial` for one met over a
proper subset of its forms, `fail` for one the shell does not meet, and `n/a`
for one that does not bind a shell without the underlying facility.

## Summary

| Status | Rows |
|---|---|
| pass | 77 |
| partial | 13 |
| fail | 11 |
| n/a | 1 |
| total | 102 |

Harness result at the head of this branch: 105 pass, 0 fail, 13 xfail.

## 2.2 Quoting

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_02 | Quoting removes the special meaning of characters or words | pass | `word.c:40` dispatches on `LITERAL` and `qotchar()`; cases `quote_single`, `quote_double_space` |
| tag_18_02_01 | A backslash preserves the literal value of the next character; backslash-newline is a line continuation removed before tokenization | pass | `word.c:134` `nextc()` folds `ESCAPE`+`NL` and masks the escaped character otherwise; cases `quote_backslash`, `quote_backslash_newline` |
| tag_18_02_02 | Single-quotes preserve every enclosed character | pass | `word.c:40` copies to the closing `LITERAL` through `qmask()`; case `quote_single` |
| tag_18_02_03 | Double-quotes preserve all but `$`, backquote and backslash | pass | `word.c:60` `qotchar()` branch; `macro.c:399` keeps expansion inside quotes; cases `quote_backslash_dollar`, `quote_backslash_dquote`, `quote_double_newline` |

The quoting encoding is the KIAE one: `qmask()` in `ctype.h:89` maps a quoted
character through `_ctype3[]` rather than setting bit 0200, so only characters
that are special in the first place carry a mark. A quoted ordinary letter is
indistinguishable from an unquoted one, which is why the here-document
delimiter rows below fail.

## 2.3 Token Recognition

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_03.p01_line_input_modes | The shell reads input a line at a time in ordinary or here-document mode | pass | `word.c:156` `readc()` over `standin->fbuf`; `io.c:163` `copy()` for here-document mode |
| tag_18_03.p02_here_document_mode | Here-document bodies start after the next newline that ends a line | pass | `cmd.c:218` queues the redirection on `iopend`; `word.c:115` drains it at `eolchar` |
| tag_18_03.p03_first_applicable_rule | The first applicable rule of the ten decides the token | partial | `word.c:11` applies the rules in the Bourne order; alias substitution (rule 5's second half) is absent |
| tag_18_03.rule01_end_of_input | End of input delimits the current token | pass | `word.c:70` `eofmeta()` ends the word loop; case `opt_c_string` |
| tag_18_03.rule02_operator_continuation | A character that can continue the current operator does so | pass | `word.c:92` `dipchar()` doubles `<<`, `>>`, `&&`, `\|\|`, `;;` |
| tag_18_03.rule03_operator_delimitation | A character that cannot continue an operator delimits it | pass | `word.c:102` pushes the lookahead back through `peekn` |
| tag_18_03.rule04_quoted_text | Quoted text is part of the current word and quoting is applied before later rules | pass | `word.c:40` and `word.c:60` run before the metacharacter test; case `quote_single` |
| tag_18_03.rule05_substitution_recursion | `$` and backquote start substitution, recursively | partial | `macro.c:119` handles `$`, `macro.c:394` the backquote; `$(` is not recognized, see 2.6.3 |
| tag_18_03.rule06_operator_start | A new operator starts at an unquoted operator character | pass | `word.c:92` `dipchar()` |
| tag_18_03.rule07_blank_delimitation | An unquoted blank delimits a word and is discarded | pass | `word.c:23` `space()` skip loop; case `split_default_ifs` |
| tag_18_03.rule08_word_continuation | Any other character continues the current word | pass | `word.c:55` |
| tag_18_03.rule09_comment | `#` starts a comment that runs to the newline | pass | `word.c:26` `COMCHAR` branch |
| tag_18_03.rule10_word_start | Any other character starts a new word | pass | `word.c:36` |
| tag_18_03.p04_grammar_categorization | Delimited tokens are categorized by the grammar | pass | `word.c:78` promotes a digit before `<` or `>` to IO_NUMBER, `word.c:85` looks up reserved words |
| tag_18_03_01.a01_command_name_substitution | The command name is replaced by its alias value | fail | no `alias` anywhere in `bin/sh` |
| tag_18_03_01.a02_trailing_blank_chain | A trailing blank in an alias value makes the next word a candidate | fail | as above |
| tag_18_03_01.a03_environment_noninheritance | Aliases are not inherited across an execution environment | n/a | no alias facility to inherit |

## 2.4 Reserved Words

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_04 | The reserved words are `!` `{` `}` `case` `do` `done` `elif` `else` `esac` `fi` `for` `if` `in` `then` `until` `while` | partial | `msg.c:89` `reserved[]` carries 15 of the 16; `!` is absent, so `! pipeline` is parsed as a command name, case `status_negation` |

## 2.5 Parameters and Variables

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_05 | A parameter is a name, a number or a special character | pass | `macro.c:134` splits the three cases |
| tag_18_05_01 | Positional parameters are assigned from the shell arguments and by `set` | pass | `args.c:54` `options()`, `xec.c` `SYSSET`; cases `param_positional`, `sb_set_positional` |
| tag_18_05_02 | `@` `*` `#` `?` `-` `$` `!` `0` expand as the standard defines | pass | `macro.c:176` through `macro.c:199`; cases `param_at_quoted`, `param_star_quoted`, `param_hash`, `param_question_true`, `param_dash_has_flags`, `param_dollar_is_decimal` |
| tag_18_05_03 | `IFS`, `PATH`, `HOME`, `PS1`, `PS2`, `MAIL` and the rest keep their defined meanings | partial | `name.c` node table and `msg.c:55` names; `ENV`, `LC_*`, `PWD` and `PPID` are absent, `msg.c:71` `defpath` is the 2.11BSD `:/bin:/usr/bin:/usr/ucb/bin:/etc` |

## 2.6 Word Expansions

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_06 | The expansions are performed in the order tilde, parameter, command substitution, arithmetic, field splitting, pathname expansion, quote removal | partial | `macro.c:110` `getch()` drives parameter and command substitution, `expand.c:57` pathname expansion, `service.c:418` `trim()` quote removal; tilde and arithmetic never run |
| tag_18_06_01 | A leading unquoted `~` is replaced by the home directory | fail | no tilde handling in `macro.c`; case `tilde_home` |
| tag_18_06_02 | `${parameter}`, `:-` `-` `:=` `=` `:?` `?` `:+` `+` | pass | `macro.c:323` `defchar()` test and `macro.c:331`; cases `expand_minus_unset`, `expand_colon_minus_null`, `expand_minus_null`, `expand_colon_equals`, `expand_colon_plus`, `expand_plus_set`, `expand_colon_question` |
| tag_18_06_02 | `${#parameter}` is the length of the value | pass | `macro.c:139` brace branch and the length conversion at `macro.c:215`; cases `expand_length`, `expand_length_unset`, `expand_length_argc`, `expand_length_star`, `expand_length_positional` |
| tag_18_06_02 | `${parameter#word}` `##` `%` `%%` remove a matching prefix or suffix | pass | `macro.c:234` reads the operator and matches the pattern with `expand.c` `gmatch()`; cases `expand_rm_smallest_suffix`, `expand_rm_largest_suffix`, `expand_rm_smallest_prefix`, `expand_rm_largest_prefix`, `expand_rm_basename`, `expand_rm_dirname`, `expand_rm_literal`, `expand_rm_quoted_result`, `expand_rm_no_match_suffix`, `expand_rm_no_match_prefix` |
| tag_18_06_03 | Command substitution in both the `$(command)` and backquote forms | partial | `macro.c:58` `comsubst()` implements the backquote form; `$(` reaches `cmd.c` as a bare `(` and is a syntax error; cases `cmdsub_backquote`, `cmdsub_backquote_nested`, `cmdsub_dollar_paren`, `cmdsub_dollar_paren_nested` |
| tag_18_06_04 | `$((expression))` evaluates an arithmetic expression | fail | no arithmetic evaluator in `bin/sh`; cases `arith_simple`, `arith_leading_paren`, `arith_vs_subshell_group`. The `$((` versus `$( (` distinction is moot because neither form parses: both stop at the first `(`. `/usr/bin/expr` in `distrib/rp2040/mi.rp2040` is the substitute a script has to use |
| tag_18_06_05 | Fields are split on IFS after expansion, and not inside quotes | pass | `macro.c:73` and `name.c:117` `mactrim()`; cases `split_default_ifs`, `split_custom_ifs`, `split_quoted_no_split` |
| tag_18_06_06 | Unquoted `*`, `?` and `[` generate pathnames, and the word is left alone when nothing matches | pass | `expand.c:57` `expand()`, `expand.c:232` `getdir()`; cases `glob_star`, `glob_question`, `glob_nomatch_literal`, `opt_f_disables_glob` |
| tag_18_06_07 | Quote characters are removed last unless they came from an expansion | pass | `service.c:418` `trim()`; case `quote_backslash_dollar` |

## 2.7 Redirection

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_07 | Redirection operators are recognized before the command runs and the file descriptor defaults to 0 or 1 | pass | `cmd.c:170` `inout()`, `word.c:78` IO_NUMBER |
| tag_18_07_01 | `<` opens the file for reading on the descriptor | pass | `cmd.c:196`, `io.c:95` `chkopen()`; case `redir_in` |
| tag_18_07_02 | `>` truncates or creates, and `>\|` overrides noclobber | partial | `cmd.c:188` sets `IOPUT`; there is no noclobber flag in `args.c:15` `flagchar[]`, and `>\|` leaves the `\|` as the next token, so it is a syntax error rather than a synonym for `>`; cases `redir_out`, `redir_clobber_override` |
| tag_18_07_03 | `>>` appends | pass | `cmd.c:191` `IOAPP`; case `redir_append` |
| tag_18_07_04 | `<<` and `<<-` read a here-document; a quoted delimiter suppresses expansion of the body | partial | `cmd.c:181` `IODOC`, `io.c:163` `copy()`, `IOSTRIP` for `<<-`; the quoted-delimiter rule fails because `nosubst` at `service.c:440` reads `isq()` of the bitwise-or of every delimiter byte, and the KIAE encoding leaves a quoted letter unmarked, so `<<'EOF'`, `<<\EOF` and `<<"EOF"` all expand the body; cases `redir_here_doc`, `redir_here_doc_strip`, `redir_here_doc_quoted` |
| tag_18_07_05 | `<&` duplicates or closes an input descriptor | pass | `cmd.c:197` `IOMOV`; case `redir_close` |
| tag_18_07_06 | `>&` duplicates or closes an output descriptor | pass | `cmd.c:197` `IOMOV`; cases `redir_dup_err_to_out`, `redir_err_to_out_order` |
| tag_18_07_07 | `<>` opens the file for reading and writing | fail | `cmd.c:199` sets `IORDW` and `defs.h:90` defines it, but no consumer reads the bit, so `<>` opens read-only and fails when the file does not exist; case `redir_readwrite` |

## 2.8 Exit Status and Errors

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_08 | The shell reports the status of the last command executed | pass | `xec.c` `exitval`, `main.c:112` `exitset()`; cases `status_exit_value`, `status_pipeline_last` |
| tag_18_08_01 | A shell error in a non-interactive shell exits it; a utility error does not | pass | `error.c:37` `exitsh()` distinguishes `forked`, `errflg` and `ttyflg`; cases `expand_colon_question`, `sb_readonly` |
| tag_18_08_02 | A command not found exits 127, a command found but not executable exits 126, and a command terminated by a signal exits above 128 | partial | `error.c:10` `failure()` with `EXNOTFOUND` and `EXNOEXEC` from `defs.h:13`, used at `xec.c:110` and `service.c:270`; cases `status_not_found`, `status_not_executable`, `status_not_found_in_pipeline`. A signal-terminated command still reports the raw wait status through `service.c:380` rather than 128 plus the signal number |

## 2.9 Shell Commands

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_09_01 | A simple command is variable assignments, redirections and words, expanded in that order | pass | `cmd.c:239` `item()`, `xec.c:88` `getarg()`/`scan()` |
| tag_18_09_01_01 | Command search tries functions, special built-ins, regular built-ins, then PATH | partial | `hashserv.c:56` `pathlook()` checks the hash, then built-ins through `syslook()`, then `findpath()`; functions and built-ins share one table, so the standard's ordering among them is not observable |
| tag_18_09_02 | A pipeline connects each command's standard output to the next command's standard input, optionally preceded by `!` | partial | `xec.c` TFIL handling; `!` is not a reserved word, see 2.4; cases `cmd_pipeline`, `status_negation` |
| tag_18_09_02_01 | A pipeline's exit status is that of its last command, negated by `!` | partial | case `status_pipeline_last` passes; the `!` half cannot be reached |
| tag_18_09_03 | Lists are commands separated by `;`, `&`, `&&`, `\|\|` and newline | pass | `cmd.c:499` `list()`, `cmd.c:518` `cmd()` |
| tag_18_09_03_02 | An asynchronous list runs in a subshell without waiting | pass | `cmd.c:538` `'&'` case, `service.c:243` `execa()` in the child |
| tag_18_09_03_04 | A sequential list runs each command in turn | pass | case `cmd_and_or` chain |
| tag_18_09_03_06 | An AND list runs the next command only if the previous one returned zero | pass | `cmd.c:499`; case `cmd_and_or` |
| tag_18_09_03_08 | An OR list runs the next command only if the previous one returned non-zero | pass | `cmd.c:499`; case `cmd_or_short` |
| tag_18_09_04_01 | `( )` runs the list in a subshell and `{ }` in the current environment | pass | `cmd.c:239` `item()`; cases `cmd_subshell`, `cmd_group` |
| tag_18_09_04_03 | The `for` loop iterates the word list, defaulting to `"$@"` | pass | `cmd.c` FORSYM, `xec.c` TFOR; case `cmd_for` |
| tag_18_09_04_05 | `case` matches patterns and `;;` ends a list | pass | `cmd.c:120` `syncase()`, `xec.c:822` `gmatch()`; cases `cmd_case`, `cmd_case_star` |
| tag_18_09_04_07 | `if` runs the consequent or the `elif`/`else` alternative | pass | `cmd.c` IFSYM/EFSYM/ELSYM; cases `cmd_if_then_else`, `cmd_if_elif` |
| tag_18_09_04_09 | `while` repeats while the condition returns zero | pass | `cmd.c` WHSYM; case `cmd_while` |
| tag_18_09_04_11 | `until` repeats while the condition returns non-zero | pass | `cmd.c` UNSYM; case `cmd_until` |
| tag_18_09_05 | A function definition names a compound command that runs in the current environment and shares the positional parameters through its own arguments | pass | `func.c`, `xec.c` TFUN; cases `cmd_function_args`, `func_shares_positional`, `func_exit_status_of_body` |
| tag_18_09_05_01 | A function's exit status is that of its last command or of `return` | pass | cases `func_exit_status_of_body`, `sb_return` |

## 2.10 Shell Grammar

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_10_01 | An operator token yields its operator identifier, a digit string before `<` or `>` yields IO_NUMBER, and anything else yields TOKEN | pass | `word.c:92` for operators, `word.c:78` for IO_NUMBER, `word.c:85` for the rest |
| tag_18_10_02 | The grammar rules recognize reserved words only in the positions the standard allows | partial | `word.c:85` gates reserved-word lookup on the `reserv` flag set by `cmd.c`; the 15-entry table misses `!`, and the `$(` and `>\|` productions have no rule at all |

## 2.11 Signals and Error Handling

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_11 | An interactive shell ignores SIGQUIT and SIGINT for asynchronous commands and honors traps | pass | `fault.c:94` `stdsigs()`, `fault.c` `sigchk()`; cases `sb_trap_exit`, `sb_trap_reset` |

## 2.12 Shell Execution Environment

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_12 | The environment consists of open files, working directory, umask, traps, variables, functions and options, inherited by a subshell and by an executed utility | pass | `service.c:255` `execs()` passes `setenvv()`; cases `sb_export`, `sb_exec_redirect`, `bi_umask` |

## 2.13 Pattern Matching Notation

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_13_01 | `?` matches any single character and a bracket expression matches one of its members | pass | `expand.c:258` `gmatch()`; cases `glob_question`, `param_dollar_is_decimal` |
| tag_18_13_02 | `*` matches any string including the empty one | pass | `expand.c:258`; case `glob_star` |
| tag_18_13_03 | A slash in a pathname is matched only by an explicit slash, and a leading period only explicitly | pass | `expand.c:91` splits on `/` before scanning, `expand.c:167` skips a leading period unless the pattern starts with one |
| n/a | Equivalence classes, character classes and collating symbols inside a bracket expression | fail | `expand.c:271` handles only literal members and ranges; there is no `[:class:]` |

## 2.14 Special Built-In Utilities

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| tag_18_14 | `break` | pass | `msg.c:143` `commands[]`, `xec.c` SYSBREAK; case `sb_break_in_loop` |
| tag_18_14 | `:` | pass | `msg.c:143`; case `sb_colon` |
| tag_18_14 | `continue` | pass | `msg.c:143`; case `cmd_continue` |
| tag_18_14 | `.` | pass | `msg.c:143`, `xec.c:143`; case `sb_dot` |
| tag_18_14 | `eval` | pass | `msg.c:143`; case `sb_eval` |
| tag_18_14 | `exec` | pass | `msg.c:143`; case `sb_exec_redirect` |
| tag_18_14 | `exit` | pass | `msg.c:143`; case `sb_exit` |
| tag_18_14 | `export` | pass | `msg.c:143`; case `sb_export` |
| tag_18_14 | `readonly` | pass | `msg.c:143`; case `sb_readonly` |
| tag_18_14 | `return` | pass | `msg.c:143`; case `sb_return` |
| tag_18_14 | `set` | pass | `msg.c:143`, `args.c:54`; case `sb_set_positional` |
| tag_18_14 | `shift` | pass | `msg.c:143`; case `sb_shift` |
| tag_18_14 | `times` | pass | `msg.c:143`; case `sb_times` |
| tag_18_14 | `trap` | pass | `msg.c:143`, `fault.c`; cases `sb_trap_exit`, `sb_trap_reset` |
| tag_18_14 | `unset` | pass | `msg.c:143`; case `sb_unset` |
| 2.14 | A special built-in's failure aborts a non-interactive shell | pass | `error.c:37` `exitsh()`; case `sb_readonly` |
| 2.14 | `command` is available as a regular built-in | fail | absent from `msg.c:143` `commands[]`; case `bi_command` |
| 2.14 | `getopts` is available as a regular built-in | fail | absent from `msg.c:143` `commands[]`; case `bi_getopts` |

All fifteen special built-ins of 2.14 are present. The table at `msg.c:143`
also carries `cd`, `echo`, `hash`, `login`, `newgrp`, `pwd`, `read`, `test`,
`[`, `type`, `ulimit`, `umask` and `wait` as regular built-ins.

## The sh utility: options

| Id | Requirement | Status | Evidence |
|---|---|---|---|
| sh | `-c` takes the command string from the next operand | pass | `args.c:104` sets `comdiv`; case `opt_c_string` |
| sh | `-i` makes the shell interactive | pass | `args.c:15` `flagchar[]` maps `i` to `intflg`, `main.c:76` |
| sh | `-s` reads commands from standard input | pass | `args.c:15` maps `STDFLG` to `stdflg`; case `opt_s_reads_stdin` |
| sh | `set -a` exports every assignment | pass | `args.c:15` `exportflg` |
| sh | `set -e` exits on an untested non-zero status | fail | `args.c:15` sets `errflg` and `main.c:112` passes `eflag` to `execute()`, but a failing simple command inside the same input does not end the shell; case `opt_e_exits_on_error` |
| sh | `set -f` disables pathname expansion | pass | `args.c:15` `nofngflg`; case `opt_f_disables_glob` |
| sh | `set -n` reads without executing | pass | `args.c:15` `noexec`; case `opt_n_no_exec` |
| sh | `set -u` treats an unset parameter as an error | pass | `args.c:15` `setflg`, `macro.c:385`; case `opt_u_unset_is_error` |
| sh | `set -v` echoes input as it is read | pass | `args.c:15` `readpr`; case `opt_v_verbose` |
| sh | `set -x` traces each command | pass | `args.c:15` `execpr`, `xec.c:104` `execprint()`; case `opt_x_traces` |
| sh | `set -b`, `-h`, `-m`, `-C`, `-o option` | fail | `args.c:15` `flagchar[]` carries `x n v t s i e r k u h f a` only; there is no `-o`, no `-C` noclobber and no job-control `-m` |
| sh | `$0` is the shell name or the `-c` command name operand | fail | `main.c` sets `cmdadr` to the whole `-c` string, so `$0` and every diagnostic prefix repeat the command text rather than a name |

## The "not found" message that lost a character

The board reported `echo hello | compress | uncompress` printing
`uncompres: not found`. Two independent questions follow: whether the
hard link resolves, and where the character went.

The link resolves. `distrib/rp2040/mi.rp2040:205` declares
`link /usr/bin/uncompress` with `target /usr/bin/compress`, and
`tools/fsutil/fsutil.c:502` `add_hardlink()` turns that into a directory entry
pointing at the same inode. Reading the built image back confirms it:

    $ tools/bin/fsutil --verbose --partition=1 distrib/rp2040/sdcard.img
    /usr/bin/compress - 12424 bytes
    /usr/bin/uncompress - 12424 bytes
    /usr/bin/zcat - 12424 bytes

The name in the message is the name handed to `execve`. `service.c:270`
passes `*t` to the diagnostic, and `execs()` at `service.c:187` passes that
same `t` to `execve` as the argument vector whose first element is `t[0]`.
The path it execs is built from `t[0]` by `catpath()` at `service.c:135`.
There is one string, so a message that names `uncompres` means the search
itself looked for `uncompres` -- a message-only truncation is not reachable
from this code.

The truncation does not reproduce. Building these sources for the host and
running the same pipeline prints the full name on every input path: through
`-c`, through a script file, and through a pty with the line editor and tab
completion engaged, where `editline()` returns the whole line and `complete()`
inserts `st.common_len - prefixlen` bytes of the sole candidate. Case
`status_not_found_in_pipeline` pins that invariant so a future regression in
the tokenizer, the hash, the PATH search or the diagnostic is caught on the
host. The remaining suspects are outside `bin/sh`: the USB CDC console input
path that `sys/arch/rp2040/compile/PICO/Makefile` selects with
`CONS_MAJOR=UARTUSB_MAJOR`, and the kernel argument copy at
`kern/exec_subr.c`. Neither was reproduced here, and no fix is claimed for
them.

## What landed on this branch

| Commit | Change |
|---|---|
| `sh: anchor the block arena at the break setbrk returns` | `blok.c` seeded `bloktop` from `&end` while `brkbegin` came from `setbrk()`; the two agree on the board because `lib/elf32-arm.ld:203` emits `_end = .; PROVIDE (end = .)` and `lib/libc/arm/sys/sbrk.c:9` starts `_curbrk` at `_end`. The host build needs them to be one value. |
| `sh: add a host POSIX conformance harness for bin/sh` | `bin/sh/tests/posix-sh.sh` with `hostshim.h` and `hostshim.c`. |
| `sh: convert a full int to decimal in itos` | `print.c` divided from 10000 down, folding the leading digits of any value at or above 100000 into one byte through `d + '0'`. XCU 2.5.2 requires `$$` to be decimal. |
| `sh: exit 127 when a command name is not found and 126 when it is` | `error.c` gains `failure()`; the command-search sites in `xec.c` and `service.c` name `EXNOTFOUND` and `EXNOEXEC` instead of exiting 1. |
| `sh: expand ${#parameter} to the length of its value` | `macro.c` read `#` only as the positional parameter count. |
| `sh: remove matching prefixes and suffixes in parameter expansion` | `macro.c` now accepts `#`, `##`, `%` and `%%` and matches with `expand.c` `gmatch()`. |

The shell grew from 28898 to 29418 bytes of text.

## Scope cuts

Each of these is a parser or evaluator addition rather than a contained
change, and none was attempted.

- `$(command)` substitution and `$((expression))` arithmetic. Both need `$(`
  recognized in `macro.c` `getch()` and a matching-paren scan; arithmetic
  needs an evaluator this shell does not have. `/usr/bin/expr` is on the
  image as the arithmetic substitute.
- `!` as a reserved word. `syslook()` at `name.c:78` binary-searches a sorted
  table, so `"!"` goes at the front of `reserved[]` and `no_reserved` goes to
  16, but `cmd.c` also has to carry the negation through the pipeline node.
- Tilde expansion, `>|`, `<>`, `alias`, `getopts`, `command`, `set -o` and
  `set -C`.
- The here-document quoted-delimiter rule, which needs a quoting
  representation that marks a quoted ordinary letter. The KIAE `_ctype3[]`
  mapping in `ctype.h:89` deliberately does not.
- `set -e`. The flag is parsed and stored; making it end the shell touches
  `execute()`'s error path for every command form.

## Noted, not fixed

`ctype.h:87` defines `char cj;` in a header, so every translation unit that
includes it emits a tentative definition of the same name. The board build
passes `-fcommon` (`share/mk/sys.mk`), which merges them; a host build without
that flag fails to link. It is a linkage defect rather than a conformance one.
