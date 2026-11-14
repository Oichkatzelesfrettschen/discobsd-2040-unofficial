#!/bin/sh
# Contract of the board's compile driver, distrib/rp2040/cc, under a POSIX sh.
#
# The driver's three tools are replaced by one stub that logs its name and
# arguments and creates the file it was asked to write, and PATH holds one
# command, an rm that logs before it removes. A driver that starts any
# other program (expr, basename, sed) therefore fails with "not found",
# which is how the launch count is held: each case's log is the complete
# list of processes the driver started. Three cases with unusual names
# (a leading dot, two dots in a row) add basename and sed to PATH, the
# route the driver keeps for them. The mutant restores the earlier stem(),
# a basename and sed pipeline for every name, and must fail.
#
# usage: check.sh DRIVER
set -eu

driver=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/bin" "$work/tools" "$work/run"
failures=0
checks=0

cat >"$work/tools/stub" <<'STUB'
#!/bin/sh
name=${0##*/}
echo "$name $*" >>"$CC_DRIVER_LOG"
case $name in
smlrc)
	case $1 in
	*fail*) exit 1 ;;
	esac
	: >"$2"
	;;
*)
	previous=
	for argument in "$@"; do
		case $previous in
		-o) : >"$argument" ;;
		esac
		previous=$argument
	done
	;;
esac
STUB
chmod +x "$work/tools/stub"
for tool in smlrc as ld; do
	ln -s stub "$work/tools/$tool"
done
real_rm=$(command -v rm)
cat >"$work/bin/rm" <<STUB
#!/bin/sh
echo "rm" >>"\$CC_DRIVER_LOG"
exec $real_rm "\$@"
STUB
chmod +x "$work/bin/rm"

# install SOURCE NAME: a copy of the driver bound to the stubs.
install() {
	sed -e "s|^crt0=.*|crt0=crt0.o|" -e "s|^libc=.*|libc=libc.a|" \
	    -e "s|^smlrc=.*|smlrc=$work/tools/smlrc|" \
	    -e "s|^as=.*|as=$work/tools/as|" \
	    -e "s|^ld=.*|ld=$work/tools/ld|" \
	    -e "s|^tmp=.*|tmp=$work/run/tmp|" "$1" >"$work/$2"
}

# expect DRIVER STATUS LOG ARGUMENT...: run the driver in the scratch
# directory and compare its exit status and its process log.
expect() {
	program=$1
	want_status=$2
	want_log=$3
	shift 3
	checks=$((checks + 1))
	: >"$work/log"
	status=0
	(cd "$work/run" && CC_DRIVER_LOG="$work/log" PATH="$work/bin" \
	    /bin/sh "$work/$program" "$@") >"$work/out" 2>&1 || status=$?
	got_log=$(tr '\n' ';' <"$work/log" | sed "s|$work/run/||g")
	if [ "$status" != "$want_status" ] || [ "$got_log" != "$want_log" ]; then
		failures=$((failures + 1))
		echo "FAIL cc $*: status $status, log [$got_log]; want $want_status, [$want_log]"
		sed 's/^/    /' "$work/out"
	fi
	leftover=$(cd "$work/run" && echo tmp.*)
	if [ "$leftover" != 'tmp.*' ]; then
		failures=$((failures + 1))
		echo "FAIL cc $*: intermediate files remain: $leftover"
	fi
}

install "$driver" driver
expect driver 0 \
    'smlrc a.c tmp.a.s;as tmp.a.s -o tmp.a.o;ld -o prog crt0.o tmp.a.o libc.a;rm;' \
    -o prog a.c
expect driver 0 'smlrc dir/x.y.c x.y.s;' -S dir/x.y.c
expect driver 0 'smlrc /abs/deep/m.c out.s;' -S -o out.s /abs/deep/m.c
expect driver 0 \
    'smlrc a.c tmp.a.s;as tmp.a.s -o a.o;as b.s -o b.o;rm;' -c a.c b.s
expect driver 0 'ld -o a.out crt0.o a.o b.o libc.a;' a.o b.o
expect driver 0 \
    'smlrc one.c tmp.one.s;as tmp.one.s -o tmp.one.o;smlrc two.c tmp.two.s;as tmp.two.s -o tmp.two.o;ld -o a.out crt0.o tmp.one.o tmp.two.o libc.a;rm;' \
    -lm one.c two.c
expect driver 1 'smlrc fail.c tmp.fail.s;rm;' fail.c
expect driver 1 ''
expect driver 1 '' -o x -c a.c b.c
expect driver 1 '' -Wall a.c
expect driver 1 '' a.x
expect driver 1 '' -o

# A name with a leading dot or two dots in a row, and a path that holds a
# pattern character (with a file in the directory that the pattern would
# match), take the basename and sed route, so those two commands join PATH for these cases; the log still
# lists every other process.
for tool in basename sed; do
	ln -s "$(command -v "$tool")" "$work/bin/$tool"
done
expect driver 0 'smlrc .hidden.c .hidden.s;' -S .hidden.c
expect driver 0 'smlrc sub/x..c x..s;' -S sub/x..c
expect driver 0 'as x..s -o x..o;' -c x..s
: >"$work/run/xa"
: >"$work/run/a1"
expect driver 0 'smlrc dir/x[ab].c x[ab].s;' -S 'dir/x[ab].c'
expect driver 0 'smlrc a[12].c a[12].s;' -S 'a[12].c'
expect driver 0 'smlrc d?r/m.c m.s;' -S 'd?r/m.c'
rm "$work/bin/basename" "$work/bin/sed"

if [ "$failures" -ne 0 ]; then
	echo "cc driver: $failures of $checks cases failed"
	exit 1
fi

# The earlier stem() starts basename and sed; with neither on PATH the
# compile cannot name its files, and the case that passed above fails.
sed 's|^\tstem "\$src"$|\tbase=`basename "$src" \| sed "s/\\\\.[cso]$//"`|' \
    "$driver" >"$work/mutant-source"
if cmp -s "$driver" "$work/mutant-source"; then
	echo "cc driver: FAIL: the mutation did not apply"
	exit 1
fi
install "$work/mutant-source" mutant
: >"$work/log"
if (cd "$work/run" && CC_DRIVER_LOG="$work/log" PATH="$work/bin" \
    /bin/sh "$work/mutant" -S a.c) >"$work/out" 2>&1 &&
    [ "$(cat "$work/log")" = 'smlrc a.c a.s' ]; then
	echo "cc driver: FAIL: a driver that starts basename and sed passed"
	exit 1
fi
echo "PASS cc driver: $checks cases with their complete process lists; the basename-and-sed mutant rejected"
