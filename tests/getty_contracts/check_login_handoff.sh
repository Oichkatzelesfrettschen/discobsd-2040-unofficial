#!/bin/sh
set -eu

source_file=${1:?usage: check_login_handoff.sh login.c}

check_handoff_order()
{
	positions=$(awk '
		index($0, "login_local_modes_clear(0, &saved_local_modes);") {
			clear_line = NR; clear_count++
		}
		index($0, "(void)alarm((u_int)0);") {
			commit_line = NR; commit_count++
		}
		index($0, "login_local_modes_restore(0, saved_local_modes);") {
			restore_line = NR; restore_count++
		}
		index($0, "if (!pflag)") {
			environment_line = NR; environment_count++
		}
		index($0, "execlp(pwd->pw_shell, tbuf, 0);") {
			exec_line = NR; exec_count++
		}
		END {
			if (clear_count != 1 || commit_count != 1 ||
			    restore_count != 1 || environment_count != 1 ||
			    exec_count != 1)
				exit 1
			print clear_line, commit_line, restore_line,
			    environment_line, exec_line
		}
	' "$1") || return 1
	set -- $positions
	[ "$1" -lt "$2" ] && [ "$2" -lt "$3" ] &&
	    [ "$3" -lt "$4" ] && [ "$4" -lt "$5" ]
}

if ! check_handoff_order "$source_file"; then
	echo "login handoff: clear, authentication commit, restore, environment and exec order differs" >&2
	exit 1
fi

temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/login-handoff.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

good_fixture=$temporary_directory/good.c
missing_fixture=$temporary_directory/missing.c
reversed_fixture=$temporary_directory/reversed.c

printf '%s\n' \
    'login_local_modes_clear(0, &saved_local_modes);' \
    '(void)alarm((u_int)0);' \
    'login_local_modes_restore(0, saved_local_modes);' \
    'if (!pflag)' \
    'execlp(pwd->pw_shell, tbuf, 0);' >"$good_fixture"
printf '%s\n' \
    'login_local_modes_clear(0, &saved_local_modes);' \
    '(void)alarm((u_int)0);' \
    'if (!pflag)' \
    'execlp(pwd->pw_shell, tbuf, 0);' >"$missing_fixture"
printf '%s\n' \
    'login_local_modes_clear(0, &saved_local_modes);' \
    'login_local_modes_restore(0, saved_local_modes);' \
    '(void)alarm((u_int)0);' \
    'if (!pflag)' \
    'execlp(pwd->pw_shell, tbuf, 0);' >"$reversed_fixture"

check_handoff_order "$good_fixture"
if check_handoff_order "$missing_fixture" ||
    check_handoff_order "$reversed_fixture"; then
	echo "login handoff: negative control passed" >&2
	exit 1
fi

echo "login handoff source order: pass"
