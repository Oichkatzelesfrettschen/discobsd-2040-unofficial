#!/bin/sh
#
# Host conformance harness for bin/sh against the POSIX Shell Command
# Language (XCU chapter 2) and the sh utility page.
#
# bin/sh/Makefile builds only for the target, so this script compiles the
# same sources for the build host itself: 32-bit, gnu89, with
# hostshim.h force-included and hostshim.c linked in.  The 32-bit
# requirement is not cosmetic -- the shell stores pointers in int
# throughout, so an LP64 build faults before it reaches its command loop.
#
# Each case feeds one snippet to the built shell and compares the exact
# bytes it writes and the exit status it reports.  A case whose POSIX
# answer this shell does not implement is declared with xfail() and
# carries its XCU section; xfail() reports XFAIL while the shell keeps
# the old answer and FAIL the moment the shell starts producing the
# POSIX one, so a landed fix forces its row to move.  Counts and a
# "SHTEST OK" line close the run.
#
# Run: sh bin/sh/tests/posix-sh.sh
# Environment: HOST_CC (default cc).

set -eu

SRCDIR=$(cd "$(dirname "$0")" && pwd)
SHSRC=$(cd "$SRCDIR/.." && pwd)
TOPSRC=$(cd "$SHSRC/../.." && pwd)
HOST_CC=${HOST_CC:-cc}
OUT=$SRCDIR/out
SH=$OUT/sh
WORK=$OUT/work

SOURCES="setbrk blok stak cmd fault main word string name args xec service
	error io print macro ctype msg test defs echo hash hashserv pwd func pwname"

CFLAGS="-m32 -std=gnu89 -O1 -w -fcommon -include $SRCDIR/hostshim.h
	-D_FILE_OFFSET_BITS=64 -DSH_HOST_COMMAND_ALLOC_FAILURE"

# blok.c defines global alloc() and free(); rename both so a libc
# internal free() never lands in the shell's own arena walker.
# The shell also owns a global variable named tmpnam; rename that host symbol
# so glibc's link-warning section does not mistake it for tmpnam(3) use.
CFLAGS="$CFLAGS -Dalloc=sh_alloc -Dfree=sh_free -Dtmpnam=sh_tmpnam"

build()
{
	rm -rf "$OUT"
	mkdir -p "$OUT" "$WORK"

	# Compiler variables include argument words, as in the compile commands below.
	# shellcheck disable=SC2086
	if [ "$(sh "$TOPSRC/tools/compiler-probe.sh" ilp32 $HOST_CC -m32)" != yes ]; then
		echo "ERROR build: $HOST_CC cannot compile and execute an ILP32 binary." >&2
		echo "bin/sh stores pointers in int; install the 32-bit" >&2
		echo "host libraries (glibc-devel.i686, gcc-multilib) and rerun." >&2
		exit 2
	fi

	$HOST_CC -m32 -std=gnu17 -Wall -Wextra -Werror \
	    -D_FILE_OFFSET_BITS=64 \
	    -c -o "$OUT/hostshim.o" "$SRCDIR/hostshim.c"
	mkdir "$WORK/dir-layout"
	: > "$WORK/dir-layout/abcde"

	# The getdents64 adapter and every consumer must agree on a directory
	# record layout that preserves Linux inode and offset widths.
	# shellcheck disable=SC2086
	$HOST_CC $CFLAGS -Wall -Wextra -Werror \
	    -o "$OUT/hostshim_layout_test" \
	    "$SRCDIR/hostshim_layout_test.c" "$OUT/hostshim.o"
	"$OUT/hostshim_layout_test" "$WORK/dir-layout"

	# expand.c reads directory blocks itself; give it a complete DIR and
	# route its one read(2) call at hostshim.c.
	# shellcheck disable=SC2086
	$HOST_CC $CFLAGS -DSH_HOST_RAWDIR -Dread=sh_host_getdents \
	    -c -o "$OUT/expand.o" "$SHSRC/expand.c"
	# edit.c already carries a termios path for its own host test.
	# shellcheck disable=SC2086
	$HOST_CC $CFLAGS -DHOSTBUILD -c -o "$OUT/edit.o" "$SHSRC/edit.c"
	for unit in $SOURCES; do
		# shellcheck disable=SC2086
		$HOST_CC $CFLAGS -c -o "$OUT/$unit.o" "$SHSRC/$unit.c"
	done
	$HOST_CC -m32 -o "$SH" "$OUT"/*.o
}

pass=0
fail=0
xfail=0
failed=""

# _run SNIPPET -- sets out and rc.  stderr is merged: several POSIX
# requirements in 2.8.1 and the sh page are about the diagnostic.
out=""
rc=0
_run()
{
	if out=$(cd "$WORK" && "$SH" -c "$1" < /dev/null 2>&1); then
		rc=0
	else
		rc=$?
	fi
}

# _run_stdin SCRIPT -- runs a complete script through the shell input stack.
_run_stdin()
{
	if out=$(cd "$WORK" && printf '%s\n' "$1" | "$SH" 2>&1); then
		rc=0
	else
		rc=$?
	fi
}

# _run_command_alloc_failure SNIPPET -- fails command environment allocation.
_run_command_alloc_failure()
{
	if out=$(cd "$WORK" && SH_TEST_COMMAND_BEGINLIST_FAIL=1 \
	    "$SH" -c "$1" < /dev/null 2>/dev/null); then
		rc=0
	else
		rc=$?
	fi
}

# ok NAME EXPECTED SNIPPET
ok()
{
	_run "$3"
	if [ "$out" = "$2" ] && [ "$rc" -eq 0 ]; then
		pass=$((pass + 1))
		echo "PASS $1"
	else
		fail=$((fail + 1))
		failed="$failed $1"
		echo "FAIL $1: want [$2] rc 0, got [$out] rc $rc"
	fi
}

# okrc NAME EXPECTED EXPECTED_RC SNIPPET
okrc()
{
	_run "$4"
	if [ "$out" = "$2" ] && [ "$rc" -eq "$3" ]; then
		pass=$((pass + 1))
		echo "PASS $1"
	else
		fail=$((fail + 1))
		failed="$failed $1"
		echo "FAIL $1: want [$2] rc $3, got [$out] rc $rc"
	fi
}

# okstdin NAME EXPECTED SCRIPT
okstdin()
{
	_run_stdin "$3"
	if [ "$out" = "$2" ] && [ "$rc" -eq 0 ]; then
		pass=$((pass + 1))
		echo "PASS $1"
	else
		fail=$((fail + 1))
		failed="$failed $1"
		echo "FAIL $1: want [$2] rc 0, got [$out] rc $rc"
	fi
}

# okalloc NAME EXPECTED SNIPPET
okalloc()
{
	_run_command_alloc_failure "$3"
	if [ "$out" = "$2" ] && [ "$rc" -eq 0 ]; then
		pass=$((pass + 1))
		echo "PASS $1"
	else
		fail=$((fail + 1))
		failed="$failed $1"
		echo "FAIL $1: want [$2] rc 0, got [$out] rc $rc"
	fi
}

# xfail NAME XCU_SECTION EXPECTED SNIPPET -- POSIX answer this shell
# does not give.  Producing it now is a failure, so the row moves.
xfail()
{
	_run "$4"
	if [ "$out" = "$3" ]; then
		fail=$((fail + 1))
		failed="$failed $1"
		echo "FAIL $1: XCU $2 now conforms; promote this case to ok()"
	else
		xfail=$((xfail + 1))
		echo "XFAIL $1 (XCU $2): want [$3], got [$out] rc $rc"
	fi
}

build

# ---- XCU 2.2 quoting ----
ok quote_single 'a  $b' "echo 'a  \$b'"
ok quote_double_space 'a b' 'echo "a b"'
ok quote_backslash 'a b' 'echo a\ b'
ok quote_backslash_dollar '$x' 'echo "\$x"'
ok quote_backslash_dquote 'a"b' 'echo "a\"b"'
ok quote_backslash_newline 'ab' 'echo a\
b'
ok quote_double_newline 'a
b' 'echo "a
b"'

# ---- XCU 2.5 parameters and special parameters ----
ok param_positional 'b c' 'set a b c; shift; echo $*'
ok param_at_quoted '2' 'set -- "p q" r; set -- "$@"; echo $#'
ok param_star_quoted '1' 'set -- p q; set -- "$*"; echo $#'
ok param_hash '0' 'echo "$#"'
ok param_question_true '0' 'true; echo $?'
ok param_question_false '1' 'false; echo $?'
ok param_dash_has_flags 'yes' 'set -f; case $- in *f*) echo yes;; *) echo no;; esac'
ok param_dollar_pid 'ok' 'echo $$ > /dev/null; echo ok'
ok param_dollar_is_decimal 'ok' 'case $$ in *[!0-9]*) echo bad;; ?*) echo ok;; esac'

# ---- XCU 2.6.1 tilde expansion ----
xfail tilde_home 2.6.1 "$HOME" 'echo ~'

# ---- XCU 2.6.2 parameter expansion ----
ok expand_minus_unset 'd' 'echo ${x-d}'
ok expand_colon_minus_null 'd' 'x=; echo ${x:-d}'
ok expand_minus_null '' 'x=; echo ${x-d}'
ok expand_colon_equals 'dd' 'echo ${x:=d}$x'
ok expand_colon_plus 'b' 'x=; echo ${x:+a}b'
ok expand_plus_set 'a' 'x=v; echo ${x+a}'
okrc expand_colon_question 'echo ${x:?boom}: x: boom' 1 'echo ${x:?boom}'
ok expand_length '3' 'x=abc; echo ${#x}'
ok expand_length_unset '0' 'echo ${#nosuchvar}'
ok expand_length_argc '2' 'set -- a b; echo ${#}'
ok expand_length_star '2' 'set -- a b; echo ${#*}'
ok expand_length_positional '3' 'set -- abc; echo ${#1}'
ok expand_rm_smallest_suffix 'a.b' 'x=a.b.c; echo ${x%.*}'
ok expand_rm_largest_suffix 'a' 'x=a.b.c; echo ${x%%.*}'
ok expand_rm_smallest_prefix 'b.c' 'x=a.b.c; echo ${x#*.}'
ok expand_rm_largest_prefix 'c' 'x=a.b.c; echo ${x##*.}'
ok expand_rm_basename 'leaf' 'x=/a/b/leaf; echo ${x##*/}'
ok expand_rm_dirname '/a/b' 'x=/a/b/leaf; echo ${x%/*}'
ok expand_rm_no_match_suffix 'a.b.c' 'x=a.b.c; echo ${x%zz}'
ok expand_rm_no_match_prefix 'a.b.c' 'x=a.b.c; echo ${x#zz}'
ok expand_rm_literal 'a.b' 'x=a.b.c; echo ${x%.c}'
ok expand_rm_quoted_result 'a b' 'x="a b.c"; echo "${x%.c}"'

# ---- XCU 2.6.3 command substitution ----
ok cmdsub_backquote 'hi' 'echo `echo hi`'
ok cmdsub_backquote_nested 'a b' 'echo `echo a` `echo b`'
xfail cmdsub_dollar_paren 2.6.3 'hi' 'echo $(echo hi)'
xfail cmdsub_dollar_paren_nested 2.6.3 'hi' 'echo $(echo $(echo hi))'

# ---- XCU 2.6.4 arithmetic expansion ----
xfail arith_simple 2.6.4 '3' 'echo $((1+2))'
xfail arith_leading_paren 2.6.4 '9' 'echo $(( (1+2)*3 ))'
xfail arith_vs_subshell_group 2.6.4 'x' 'echo $( (echo x) )'

# ---- XCU 2.6.5 field splitting ----
ok split_default_ifs '2' 'x="a b"; set -- $x; echo $#'
ok split_custom_ifs 'a
b
c' 'IFS=:; v=a:b:c; for w in $v; do echo $w; done'
ok split_quoted_no_split '1' 'x="a b"; set -- "$x"; echo $#'

# ---- XCU 2.6.6 pathname expansion ----
ok glob_star 'g1 g2' 'rm -f g1 g2; : > g1; : > g2; echo g[12]*'
ok glob_question 'g1 g2' 'rm -f g1 g2; : > g1; : > g2; echo g?'
ok glob_nomatch_literal 'nosuch*' 'echo nosuch*'

# ---- XCU 2.7 redirection ----
ok redir_out 'w' 'echo w > f1; cat f1'
ok redir_append 'w
a' 'echo w > f2; echo a >> f2; cat f2'
ok redir_in 'r' 'echo r > f3; cat < f3'
ok redir_here_doc 'hi' 'cat <<EOF
hi
EOF'
ok redir_here_doc_strip 'ind' 'cat <<-EOF
	ind
	EOF'
ok redir_here_doc_backslash '$x' 'x=expanded; cat <<\EOF
$x
EOF'
ok redir_here_doc_single '$x
`echo substituted`' "x=expanded; cat <<'EOF'
\$x
\`echo substituted\`
EOF"
ok redir_here_doc_double '$x' 'x=expanded; cat <<"EOF"
$x
EOF'
ok redir_here_doc_partial '$x' "x=expanded; cat <<E'OF'
\$x
EOF"
ok redir_here_doc_empty_quote '$x' "x=expanded; cat <<E''OF
\$x
EOF"
ok redir_here_doc_literal_dollar '$x' "x=expanded; tag=wrong; cat <<'E\$tag'
\$x
E\$tag"
ok redir_here_doc_quoted_strip '$x' 'x=expanded; cat <<-\EOF
	$x
	EOF'
ok redir_here_doc_unquoted_expands 'expanded
substituted' 'x=expanded; cat <<EOF
$x
`echo substituted`
EOF'
ok redir_dup_err_to_out 'e' 'echo e 1>&2 2>&1'
ok redir_err_to_out_order 'x' 'echo x 2>/dev/null'
xfail redir_readwrite 2.7.7 'ok' ': > f4; exec 3<>f4; echo ok >&3; exec 3<&-; cat f4'
ok redir_close 'ok' 'exec 3>f5; exec 3<&-; echo ok'
xfail redir_clobber_override 2.7.2 'c' 'echo c >| f6; cat f6'

# ---- XCU 2.8.1 consequences of shell errors, sh utility exit status ----
okrc status_not_found 'nosuchcmd_xyz 2>/dev/null: nosuchcmd_xyz: not found' 127 \
    'nosuchcmd_xyz 2>/dev/null'
okrc status_not_executable '' 126 ': > f7; chmod 644 f7; exec ./f7 2>/dev/null'
ok status_not_found_in_pipeline \
    'echo hi | cat | qquncompress 2>&1 | tail -1: qquncompress: not found' \
    'echo hi | cat | qquncompress 2>&1 | tail -1'
# XCU 2.8.1: command not found is not a shell error that ends the list.
# The diagnostic is the shell's own, written before any fork, so the
# command's 2>/dev/null does not catch it and it leads the output.
ok status_not_found_list_continues \
    'nosuchcmd_xyz 2>/dev/null; echo after $?: nosuchcmd_xyz: not found
after 127' \
    'nosuchcmd_xyz 2>/dev/null; echo after $?'
ok status_not_found_and_list \
    'nosuchcmd_xyz 2>/dev/null && echo no; echo $?: nosuchcmd_xyz: not found
127' \
    'nosuchcmd_xyz 2>/dev/null && echo no; echo $?'
ok status_not_found_or_list \
    'nosuchcmd_xyz 2>/dev/null || echo yes: nosuchcmd_xyz: not found
yes' \
    'nosuchcmd_xyz 2>/dev/null || echo yes'
# set -e does not act in this shell (opt_e_exits_on_error below), and a
# command not found is no exception; cmdfail() defers to failure() under
# errflg, so this row moves the day set -e itself does.
xfail status_not_found_errexit 2.8.1 \
    'set -e; nosuchcmd_xyz 2>/dev/null; echo not-reached: nosuchcmd_xyz: not found' \
    'set -e; nosuchcmd_xyz 2>/dev/null; echo not-reached'

# ---- XCU 2.8.2 exit status ----
ok status_exit_value '7' 'sh -c "exit 7" > /dev/null 2>&1 || echo $?'
ok status_pipeline_last '0' 'false | true; echo $?'
ok status_function_return '3' 'f(){ return 3; }; f; echo $?'
xfail status_negation 2.9.2 '0' '! false; echo $?'

ok status_signal_terminated 'ok' \
    'exec 2>/dev/null; sh -c "kill -9 \$\$"; case $? in 137) echo ok;; *) echo bad;; esac'

# ---- XCU 2.9 shell commands ----
ok cmd_and_or 'y' 'true && echo y || echo n'
ok cmd_or_short 'n' 'false && echo y || echo n'
ok cmd_pipeline '3' 'echo hi | wc -c | tr -d " "'
ok cmd_subshell 'sub' '(echo sub)'
ok cmd_group 'grp' '{ echo grp; }'
ok cmd_for '1
2' 'for i in 1 2; do echo $i; done'
ok cmd_while '1
2' 'i=; for j in a b; do i=x$i; case $i in x) echo 1;; xx) echo 2;; esac; done'
ok cmd_until 'ok' 'until true; do echo no; done; echo ok'
ok cmd_if_then_else 't' 'if true; then echo t; else echo f; fi'
ok cmd_if_elif 'e' 'if false; then echo t; elif true; then echo e; else echo f; fi'
ok cmd_case 'm' 'case a in b) echo b;; a) echo m;; esac'
ok cmd_case_star 'd' 'case zz in a) echo a;; *) echo d;; esac'
ok cmd_function_args 'p' 'f(){ echo $1; }; f p'
ok func_beats_regular_builtin 'mycd' 'cd(){ echo mycd; }; cd /'
ok cmd_break 'ok' 'for i in 1 2 3; do break; done; echo ok'
ok cmd_continue '2' 'for i in 1 2; do case $i in 1) continue;; esac; echo $i; done'

# ---- XCU 2.9.5 function definition ----
ok func_shares_positional '1' 'f(){ echo $#; }; set -- a b; f q'
ok func_exit_status_of_body '0' 'f(){ true; }; f; echo $?'

# ---- XCU 2.14 special built-ins ----
ok sb_colon '0' ': ; echo $?'
ok sb_dot 'ok' 'echo echo ok > d1; . ./d1'
ok sb_eval 'ev' 'eval "echo ev"'
ok sb_exec_redirect 'x' '(exec > f8; echo x); cat f8'
okrc sb_exit '' 5 'exit 5'
ok sb_export 'v' 'x=v; export x; sh -c "echo \$x"'
okrc sb_readonly 'x=1; readonly x; x=2 2>/dev/null: x: is read only' 1 \
    'x=1; readonly x; x=2 2>/dev/null'
ok sb_return '3' 'f(){ return 3; }; f; echo $?'
ok sb_set_positional 'a b' 'set a b; echo $*'
ok sb_shift 'b' 'set a b; shift; echo $1'
ok sb_times 'ok' 'times > /dev/null; echo ok'
ok sb_trap_exit 'hi
bye' 'trap "echo bye" 0; echo hi'
ok sb_trap_reset 'hi' 'trap "echo bye" 0; trap 0; echo hi'
ok sb_unset 'unset' 'x=1; unset x; echo "${x-unset}"'
ok sb_break_in_loop 'ok' 'for i in 1; do break; done; echo ok'

# ---- XCU 2.14 regular built-ins this shell carries ----
ok bi_test '0' 'test a = a; echo $?'
ok bi_bracket '0' '[ a = a ]; echo $?'
ok bi_read 'rv' 'echo rv > f9; read v < f9; echo $v'
ok bi_pwd_matches 'same' 'p=`pwd`; case $p in /*) echo same;; *) echo other;; esac'
ok bi_umask 'ok' 'umask > /dev/null; echo ok'
ok bi_type_builtin 'echo is a shell builtin' 'type echo'
ok bi_type_special_builtin 'export is a special shell builtin' 'type export'
ok bi_type_function 'fcmd is a function' 'fcmd(){ :; }; type fcmd'
ok bi_type_keyword 'if is a shell keyword' 'type if'
ok bi_type_hashed 'cat is hashed (/bin/cat)' \
    'PATH=/bin:/usr/bin; hash cat; type cat'
ok bi_type_missing 'nosuchcmd_xyz not found' 'type nosuchcmd_xyz'

# command executes after removing only the function layer from lookup.  Its
# reporting modes retain that layer so scripts can inspect the actual lookup
# order without changing PATH or executing the result.
ok bi_command_exec 'hi' 'command echo hi'
ok bi_command_double_dash 'hi' 'command -- echo hi'
ok bi_command_nested 'hi' 'command command echo hi'
ok bi_command_nested_prefix_assignment 'hi
old' \
    'x=old; x=new command command echo hi; echo "$x"'
okrc bi_command_nested_cleanup '' 1 \
    '(command command :; export 1bad=x 2>/dev/null; echo unreachable)'
ok bi_command_empty '0' 'command; echo $?'
ok bi_command_bypass_function 'external' \
    'echo "echo external" > fcmd; chmod 755 fcmd; PATH=.:/bin:/usr/bin; fcmd(){ echo function; }; command fcmd'
ok bi_command_v_builtin 'echo' 'command -v echo'
ok bi_command_V_builtin 'echo is a shell builtin' 'command -V echo'
ok bi_command_V_special_builtin 'export is a special shell builtin' \
    'command -V export'
ok bi_command_function_beats_regular_builtin 'function
wrapped' \
    'echo(){ /bin/echo function; }; echo plain; command echo wrapped'
ok bi_command_special_beats_function 'special' \
    'command_order=special; export(){ echo function; }; export command_order; echo "$command_order"'
ok bi_command_V_shadowed_builtin 'echo is a function' \
    'echo(){ :; }; command -V echo'
ok bi_command_v_function 'fcmd' 'fcmd(){ :; }; command -v fcmd'
ok bi_command_V_function 'fcmd is a function' 'fcmd(){ :; }; command -V fcmd'
ok bi_command_v_keyword 'if' 'command -v if'
ok bi_command_V_keyword 'if is a shell keyword' 'command -V if'
ok bi_command_v_path "$WORK/./xcmd" \
    'echo "exit 0" > xcmd; chmod 755 xcmd; PATH=.:/bin:/usr/bin; command -v xcmd'
ok bi_command_V_path "xcmd is $WORK/./xcmd" \
    'echo "exit 0" > xcmd; chmod 755 xcmd; PATH=.:/bin:/usr/bin; command -V xcmd'
ok bi_command_v_explicit_path "$WORK/./xcmd" 'command -v ./xcmd'
ok bi_command_p_default_path 'ok' \
    'echo "echo bad" > cat; chmod 755 cat; PATH=.; command -p cat /dev/null; echo ok'
ok bi_command_p_preserves_path '.' \
    'echo "echo bad" > cat; chmod 755 cat; PATH=.; command -p cat /dev/null; echo "$PATH"'
ok bi_command_prefix_path 'prefix-path' \
    'mkdir command-bin; echo "echo prefix-path" > command-bin/pathcmd; chmod 755 command-bin/pathcmd; PATH=command-bin command pathcmd'
ok bi_command_p_overrides_prefix_path 'ok' \
    'echo "echo bad" > cat; chmod 755 cat; PATH=. command -p cat /dev/null; echo ok'
ok bi_command_v_missing '1' 'command -v nosuchcmd_xyz; echo $?'
okrc bi_command_missing \
    'command nosuchcmd_xyz: nosuchcmd_xyz: not found' 127 \
    'command nosuchcmd_xyz'
okrc bi_command_not_executable '' 126 \
    'echo bad > noexec; chmod 644 noexec; command ./noexec 2>/dev/null'
ok bi_command_regularizes_builtin_error 'after:1' \
    'command export 1bad=x 2>/dev/null; status=$?; echo "after:$status"'
okrc bi_command_bad_option_continues 'after:1' 1 \
    'command -z 2>/dev/null; status=$?; echo "after:$status"; exit $status'
ok bi_command_missing_redirection 'after:127' \
    'command nosuchcmd_xyz 2>/dev/null; echo "after:$?"'
ok bi_command_external_missing_continues 'after:127' \
    'command /definitely-absent-shell-review 2>/dev/null; echo "after:$?"'
ok bi_command_enoexec_error_continues 'after:1' \
    'echo "echo external" > command-no-shebang; chmod 755 command-no-shebang; echo(){ shift bad; }; command ./command-no-shebang 2>/dev/null; /bin/echo "after:$?"'
ok bi_command_partial_redirection_unwind 'after' \
    'command : > command-output 2>/dev/null </definitely-absent-shell-review; echo after'
okstdin bi_command_nested_input_unwind 'after
end' 'command eval "shift x" 2>/dev/null
echo after
echo end'
okalloc bi_command_allocation_failure_unwind 'after:1' \
    'command :; echo "after:$?"'
ok bi_command_prefix_assignment 'hi
old' \
    'x=old; x=new command echo hi; echo "$x"'
ok bi_command_prefix_expansion_snapshot 'changed' \
    'x=; z=${x:=changed} x=temp command :; echo "$x"'
ok bi_command_prefix_assignment_error 'old' \
    'x=old; x=new command export 1bad=x 2>/dev/null; echo "$x"'
ok bi_command_prefix_expands_once_builtin 'observed
done' \
    'x=`echo observed >&2` command :; echo done'
ok bi_command_prefix_expands_once_external 'observed
done' \
    'x=`echo observed >&2` command /bin/true; echo done'
ok bi_command_partial_assignment_unwind 'x=old; MAILCHECK=abc x=new command :; echo "x=$x"; x=outer command eval '\''x=explicit'\''; echo "x=$x": abc: bad number
x=old
x=explicit' \
    'x=old; MAILCHECK=abc x=new command :; echo "x=$x"; x=outer command eval '\''x=explicit'\''; echo "x=$x"'
ok bi_command_prefix_keeps_export 'temp
old' \
    'x=old; export x; x=temp command eval '\''/usr/bin/printenv x'\''; echo "$x"'
ok bi_command_nested_p_default_path 'status:0' \
    'PATH=/does-not-exist; command -p command cat /dev/null; echo status:$?'
ok bi_command_special_effect_persists 'visible' \
    'command_visible=visible; command export command_visible; sh -c "echo \$command_visible"'
ok bi_command_prefix_export_effect 'old' \
    'x=old; x=new command export x; sh -c "echo \$x"'
ok bi_command_prefix_read_effect 'explicit' \
    'echo explicit > command-input; x=old; x=temporary command read x < command-input; echo "$x"'
ok bi_command_nested_prefix_read_effect 'explicit' \
    'echo explicit > command-input; x=old; x=outer command eval '\''x=inner command read x < command-input'\''; echo "$x"'
ok bi_command_prefix_readonly_effect 'old
1' \
    'x=old; x=new command readonly x; export x; sh -c "echo \$x"; (x=after) 2>/dev/null; echo "$?"'
ok bi_command_prefix_unset_effect 'unset' \
    'x=old; x=new command unset x; echo "${x-unset}"'
ok bi_command_prefix_function_collision 'f is a function
function
unset' \
    'f(){ echo function; }; f=new command export f; command -V f; f; sh -c '\''echo "${f-unset}"'\'''
ok bi_command_eval_cleanup 'ok' \
    'command eval "shift x" 2>/dev/null; command eval "echo ok"'
okrc bi_command_function_cleanup '' 1 \
    '(f(){ shift x; }; command eval f 2>/dev/null; return 7 2>/dev/null)'
okrc bi_command_bad_option \
    'command: bad option(s)' 1 \
    '(command -z 2>command.err); status=$?; cat command.err; exit $status'

# getopts publishes its cursor on every call.  The tests use literal expected
# transitions so a stale OPTIND, stale OPTARG, or wrong cluster branch cannot
# satisfy them through the parser's own bookkeeping.
ok bi_getopts_initial_optind '1' 'echo "$OPTIND"'
ok bi_getopts_simple '0|a|2|unset' \
    'set -- -a; getopts a option; echo "$?|$option|$OPTIND|${OPTARG-unset}"'
ok bi_getopts_cluster '0|a|2
0|b|2
1|2' \
    'set -- -ab; getopts ab option; echo "$?|$option|$OPTIND"; getopts ab option; echo "$?|$option|$OPTIND"; getopts ab option; echo "$?|$OPTIND"'
ok bi_getopts_attached_operand '0|a|2|unset
0|b|2|value' \
    'set -- -abvalue; getopts ab: option; echo "$?|$option|$OPTIND|${OPTARG-unset}"; getopts ab: option; echo "$?|$option|$OPTIND|$OPTARG"'
ok bi_getopts_separate_operand '0|b|3|value' \
    'set -- -b value tail; getopts b: option; echo "$?|$option|$OPTIND|$OPTARG"'
ok bi_getopts_double_dash '0|a|2
1|3' \
    'set -- -a -- -b; getopts ab option; echo "$?|$option|$OPTIND"; getopts ab option; echo "$?|$OPTIND"'
ok bi_getopts_end_status '1|1' \
    'set -- operand; getopts a option; echo "$?|$OPTIND"'
ok bi_getopts_illegal_option 'getopts: illegal option -- z
0|?|unset|2' \
    'set -- -z; getopts a option 2>getopts.err; status=$?; cat getopts.err; echo "$status|$option|${OPTARG-unset}|$OPTIND"'
ok bi_getopts_missing_operand 'getopts: option requires an argument -- a
0|?|unset|2' \
    'set -- -a; getopts a: option 2>getopts.err; status=$?; cat getopts.err; echo "$status|$option|${OPTARG-unset}|$OPTIND"'
ok bi_getopts_silent_illegal '0|?|z|2' \
    'set -- -z; getopts :a option 2>getopts.err; status=$?; cat getopts.err; echo "$status|$option|$OPTARG|$OPTIND"'
ok bi_getopts_silent_missing '0|:|a|2' \
    'set -- -a; getopts :a: option 2>getopts.err; status=$?; cat getopts.err; echo "$status|$option|$OPTARG|$OPTIND"'
ok bi_getopts_explicit_arguments '0|b|2' \
    'set -- -a; getopts ab option -b; echo "$?|$option|$OPTIND"'
ok bi_getopts_repeated_explicit 'a|b|2' \
    'args=-ab; getopts ab option $args; first=$option; getopts ab option $args; echo "$first|$option|$OPTIND"'
ok bi_getopts_repeated_after_set 'a|b|2' \
    'set -- -ab; getopts ab option; first=$option; set -f; getopts ab option; echo "$first|$option|$OPTIND"'
ok bi_getopts_reset_mid_cluster 'a|a|2' \
    'set -- -ab; getopts ab option; first=$option; OPTIND=1; getopts ab option; echo "$first|$option|$OPTIND"'
ok bi_getopts_end_clears_state '0|a|value|3
1|?|unset|3' \
    'set -- -a value; getopts a: option; echo "$?|$option|$OPTARG|$OPTIND"; getopts a: option; echo "$?|$option|${OPTARG-unset}|$OPTIND"'
ok bi_getopts_operand_clears_state '1|?|unset|1' \
    'set -- operand; option=old; OPTARG=old; getopts a option; echo "$?|$option|${OPTARG-unset}|$OPTIND"'
ok bi_getopts_dashdash_clears_state '1|?|unset|2' \
    'set -- -- tail; option=old; OPTARG=old; getopts a option; echo "$?|$option|${OPTARG-unset}|$OPTIND"'
ok bi_getopts_error_loop '?|z
a|unset
end|2|?|unset' \
    'set -- -za; while getopts :a option; do echo "$option|${OPTARG-unset}"; done; echo "end|$OPTIND|$option|${OPTARG-unset}"'
ok bi_getopts_invalid_destination 'invalid:2|1' \
    'OPTIND=1; getopts a 1bad -a 2>/dev/null; echo "invalid:$?|$OPTIND"'
ok bi_getopts_readonly_destination 'readonly:2|1' \
    'OPTIND=1; readonly result; getopts a result -a 2>/dev/null; echo "readonly:$?|$OPTIND"'
ok bi_getopts_readonly_optind 'optind:2|1' \
    'OPTIND=1; readonly OPTIND; getopts a result -a 2>/dev/null; echo "optind:$?|$OPTIND"'
ok bi_getopts_readonly_optarg 'optarg:2|1|old' \
    'OPTIND=1; OPTARG=old; readonly OPTARG; getopts a result -a 2>/dev/null; echo "optarg:$?|$OPTIND|$OPTARG"'

# ---- sh utility options ----
ok opt_c_string 'c' 'echo c'
ok opt_c_command_name 'myname arg1' 'sh -c "echo \$0 \$1" myname arg1'
ok opt_s_reads_stdin 'sfromstdin' 'echo "echo sfromstdin" | sh -s'
ok opt_x_traces '+ echo t 
t' 'set -x; echo t'
ok opt_v_verbose 'ok' 'set -v; echo ok 2>/dev/null'
okrc opt_u_unset_is_error \
    'set -u; echo ${nosuchvar} 2>/dev/null: nosuchvar: parameter not set' 1 \
    'set -u; echo ${nosuchvar} 2>/dev/null'
ok opt_f_disables_glob 'g*' 'set -f; rm -f g1 g2; : > g1; echo g*'
ok opt_n_no_exec 'ok' 'sh -n -c "echo never" ; echo ok'
xfail opt_e_exits_on_error sh '' 'set -e; false; echo notreached'

echo "--"
echo "pass $pass  fail $fail  xfail $xfail"
if [ "$fail" -ne 0 ]; then
	echo "failing:$failed"
	echo "SHTEST FAILED"
	exit 1
fi
echo "SHTEST OK"
