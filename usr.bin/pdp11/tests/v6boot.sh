#!/bin/sh
# Boot the shipped V6 pack through a host build of pdp11 on a pty: the
# bootstrap prompts, the kernel comes up multi-user, root logs in, and
# the shell, ed, a pipeline and a disk write all answer. Exit status is
# ptyrun's: 0 when every expect matched.
#
#   sh tests/v6boot.sh ./pdp11-host
set -eu
PYTHON=${PYTHON:-python3}
here=$(cd "$(dirname "$0")" && pwd)
emu=${1:?usage: v6boot.sh EMULATOR}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gunzip -c "$here/../v6.rk.gz" > "$work/root.rk"
$PYTHON "$here/ptyrun.py" -t 60 -- "$emu" "$work/root.rk" > "$work/out" <<'EOF'
expect @
send unix\r
expect login:
send root\r
expect #
send echo hello from v6\r
expect hello from v6
expect #
send ls /bin | wc\r
expect #
send echo one two > /tmp/x; cat /tmp/x; rm /tmp/x\r
expect one two
expect #
send ed\r
send a\rline one\r.\rw /tmp/e\rq\r
expect #
send cat /tmp/e; rm /tmp/e\r
expect line one
expect #
send df /dev/rk0; sync\r
expect #
send \x1f
expect exit
EOF
status=$?
cat "$work/out"
exit $status
