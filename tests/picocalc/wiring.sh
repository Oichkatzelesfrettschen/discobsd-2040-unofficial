#!/bin/sh
# Check that the keyboard poller feeds the raw key event queue.
#
# usage: wiring.sh [-m] PICOKBD.C
#
# kbd_poll() must hand each controller event to picokbd_event(), which queues
# it and translates it, and must not call picokbd_translate() itself: a poller
# that translates directly fills the tty and leaves LCDIOC_GETKEY with no
# events. Without -m the shipped file must pass. With -m a copy whose call is
# replaced by picokbd_translate() must fail the same check, which calibrates
# it against a known-bad input.
set -eu

mutate=0
if [ "$1" = -m ]; then
	mutate=1
	shift
fi
src=$1

# The text of kbd_poll() from its definition to the closing brace.
body() {
	sed -n '/^kbd_poll(/,/^}/p' "$1"
}

check() {
	text=$(body "$1")
	printf '%s\n' "$text" | grep -q 'picokbd_event(' || return 1
	if printf '%s\n' "$text" | grep -q 'picokbd_translate('; then
		return 1
	fi
	return 0
}

if [ "$mutate" -eq 0 ]; then
	if ! check "$src"; then
		echo "wiring.sh: kbd_poll() in $src does not route events through picokbd_event()" >&2
		exit 1
	fi
	echo "wiring ok: $src"
	exit 0
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/picocalc-wiring.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed 's/picokbd_event(/picokbd_translate(/' "$src" >"$work/picokbd.c"
if cmp -s "$src" "$work/picokbd.c"; then
	echo "wiring.sh: the mutation changed nothing in $src" >&2
	exit 1
fi
if check "$work/picokbd.c"; then
	echo "wiring.sh: the check accepted a poller that translates directly" >&2
	exit 1
fi
echo "mutant rejected: picokbd.c poller translates directly"
