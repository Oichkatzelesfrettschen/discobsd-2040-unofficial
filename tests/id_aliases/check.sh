#!/bin/sh
set -eu

fail()
{
	echo "id alias test: $1" >&2
	exit 1
}

output=$(./whoami)
[ "$output" = "effective-user" ] || fail "whoami effective identity"

output=$(./groups)
[ "$output" = "primary extra" ] || fail "groups current membership"

output=$(./groups alice)
[ "$output" = "staff extra" ] || fail "groups named membership"

output=$(./logname)
[ "$output" = "session-login" ] || fail "logname session identity"

output=$(./id_host -un)
[ "$output" = "effective-user" ] || fail "standalone id behavior"

if output=$(./whoami extra 2>&1); then
	fail "whoami accepts an operand"
fi
case "$output" in
*"usage: whoami"*) ;;
*) fail "whoami usage diagnostic" ;;
esac

if output=$(./groups alice extra 2>&1); then
	fail "groups accepts two operands"
fi
case "$output" in
*"usage: groups [user]"*) ;;
*) fail "groups usage diagnostic" ;;
esac

if output=$(TEST_GETLOGIN_FAIL=1 ./logname 2>&1); then
	fail "logname accepts a missing login session"
fi
case "$output" in
*"getlogin"*) ;;
*) fail "logname failure diagnostic" ;;
esac

echo "id alias tests passed"
