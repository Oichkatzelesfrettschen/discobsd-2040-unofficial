#!/bin/sh
# Check that the kernel glue of the raw key event session is connected.
#
# usage: wiring.sh [-m] PICOKBD.C LCDCONS.C VFS_VNOPS.C
#
# picokbd_evq.c is tested directly; these checks cover the calls that no host
# program executes, by reading the text of the function that must make them:
#
#   kbd_poll()         hands each event to picokbd_event(), which queues and
#                      translates it, never to picokbd_translate() alone, and
#                      tells the queue the tty's foreground group first;
#   lcdcons_fileclose() ends the session of the closing file;
#   lcd_keyevs(), lcd_getkey()  pass the descriptor's FREAD flag and the
#                      caller's process group to the queue;
#   vn_closefile()     calls lcdcons_fileclose().
#
# Without -m the shipped files must pass. With -m a copy with each of the
# connections cut in turn must fail the same check, which calibrates it
# against known-bad input.
set -eu

mutate=0
if [ "$1" = -m ]; then
	mutate=1
	shift
fi
poll=$1
cons=$2
vfs=$3

# The text of function $2 of file $1, from its definition to the closing brace.
body() {
	sed -n "/^$2(/,/^}/p" "$1"
}

# has FILE FUNCTION TEXT: the function's body contains TEXT.
has() {
	body "$1" "$2" | grep -qF -- "$3"
}

check() {
	_poll=$1
	_cons=$2
	_vfs=$3
	has "$_poll" kbd_poll 'picokbd_event(' || return 1
	has "$_poll" kbd_poll 'picokbd_evq_foreground(' || return 1
	if has "$_poll" kbd_poll 'picokbd_translate('; then
		return 1
	fi
	has "$_cons" lcdcons_fileclose 'picokbd_evq_fileclose(' || return 1
	has "$_cons" lcd_keyevs 'picokbd_evq_start(' || return 1
	has "$_cons" lcd_keyevs 'picokbd_evq_stop(' || return 1
	has "$_cons" lcd_keyevs 'p_pgrp' || return 1
	has "$_cons" lcd_keyevs 'FREAD' || return 1
	has "$_cons" lcd_getkey 'picokbd_evq_drain(' || return 1
	has "$_cons" lcd_getkey 'p_pgrp' || return 1
	has "$_cons" lcd_getkey 'FREAD' || return 1
	has "$_vfs" vn_closefile 'lcdcons_fileclose(' || return 1
	return 0
}

if [ "$mutate" -eq 0 ]; then
	if ! check "$poll" "$cons" "$vfs"; then
		echo "wiring.sh: the raw key event session is not wired in $poll, $cons and $vfs" >&2
		exit 1
	fi
	echo "wiring ok: $poll $cons $vfs"
	exit 0
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/picocalc-wiring.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# mutate NAME FILE-VARIABLE SED: apply the edit to a copy and require the check to fail.
mutant() {
	_name=$1
	_which=$2
	_expr=$3
	cp "$poll" "$work/poll.c"
	cp "$cons" "$work/cons.c"
	cp "$vfs" "$work/vfs.c"
	case $_which in
	poll) _target=$work/poll.c ;;
	cons) _target=$work/cons.c ;;
	vfs) _target=$work/vfs.c ;;
	esac
	cp "$_target" "$work/before"
	sed "$_expr" "$work/before" >"$_target"
	if cmp -s "$work/before" "$_target"; then
		echo "wiring.sh: mutant $_name changed nothing" >&2
		exit 1
	fi
	if check "$work/poll.c" "$work/cons.c" "$work/vfs.c"; then
		echo "wiring.sh: the check accepted mutant $_name" >&2
		exit 1
	fi
	echo "mutant rejected: wiring $_name"
}

mutant poller-translates poll 's/picokbd_event(/picokbd_translate(/'
mutant poller-skips-foreground poll 's/picokbd_evq_foreground(/(void)(/'
mutant close-hook-missing vfs 's/lcdcons_fileclose(fp);/(void)fp;/'
mutant fileclose-ends-nothing cons 's/picokbd_evq_fileclose(fp);/(void)fp;/'
mutant requests-ignore-fread cons 's/(flag & FREAD) != 0/1/'
mutant requests-ignore-group cons 's/u\.u_procp->p_pgrp/0/'
