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
HOST_CC=${HOST_CC:-cc}
OUT=$SRCDIR/out
SH=$OUT/sh
WORK=$OUT/work

SOURCES="setbrk blok stak cmd fault main word string name args xec service
	error io print macro ctype msg test defs echo hash hashserv pwd func"

CFLAGS="-m32 -std=gnu89 -O1 -w -fcommon -include $SRCDIR/hostshim.h"

# blok.c defines global alloc() and free(); rename both so a libc
# internal free() never lands in the shell's own arena walker.
CFLAGS="$CFLAGS -Dalloc=sh_alloc -Dfree=sh_free"

build()
{
	rm -rf "$OUT"
	mkdir -p "$OUT" "$WORK"

	if ! printf 'int main(void){return 0;}\n' > "$OUT/probe.c" ||
	    ! $HOST_CC -m32 -o "$OUT/probe" "$OUT/probe.c" 2>/dev/null; then
		echo "FAIL build: $HOST_CC cannot produce a 32-bit binary." >&2
		echo "bin/sh stores pointers in int; install the 32-bit" >&2
		echo "host libraries (glibc-devel.i686, gcc-multilib) and rerun." >&2
		exit 1
	fi

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
	$HOST_CC -m32 -w -c -o "$OUT/hostshim.o" "$SRCDIR/hostshim.c"
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
xfail expand_rm_smallest_suffix 2.6.2 'a.b' 'x=a.b.c; echo ${x%.*}'
xfail expand_rm_largest_suffix 2.6.2 'a' 'x=a.b.c; echo ${x%%.*}'
xfail expand_rm_smallest_prefix 2.6.2 'b.c' 'x=a.b.c; echo ${x#*.}'
xfail expand_rm_largest_prefix 2.6.2 'c' 'x=a.b.c; echo ${x##*.}'

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
xfail redir_here_doc_quoted 2.7.4 '$x' 'cat <<\EOF
$x
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

# ---- XCU 2.8.2 exit status ----
ok status_exit_value '7' 'sh -c "exit 7" > /dev/null 2>&1 || echo $?'
ok status_pipeline_last '0' 'false | true; echo $?'
ok status_function_return '3' 'f(){ return 3; }; f; echo $?'
xfail status_negation 2.9.2 '0' '! false; echo $?'

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
xfail bi_getopts 2.14 '0' 'set -- -a; getopts a o; echo $?'
xfail bi_command 2.14 'hi' 'command echo hi'

# ---- sh utility options ----
ok opt_c_string 'c' 'echo c'
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
