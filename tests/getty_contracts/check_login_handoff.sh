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
		index($0, "char *domain, *salt, *envinit[1] = { NULL }") {
			environment_seed_line = NR; environment_seed_count++
		}
		index($0, "if (strlen(*argv) > UT_NAMESIZE)") {
			username_bound_line = NR; username_bound_count++
		}
		index($0, "username = *argv;") {
			username_line = NR; username_count++
		}
		index($0, "if (setgid(pwd->pw_gid) < 0)") {
			setgid_line = NR; setgid_count++
		}
		index($0, "if (initgroups(username, pwd->pw_gid) < 0)") {
			initgroups_line = NR; initgroups_count++
		}
		index($0, "if (setuid(pwd->pw_uid) < 0)") {
			setuid_line = NR; setuid_count++
		}
		index($0, "if (setreuid(geteuid(), pwd->pw_uid) < 0)") {
			setreuid_count++
		}
		index($0, "if (setuid(0) < 0)") {
			root_restore_count++
		}
		index($0, "execlp(pwd->pw_shell, tbuf, (char *)NULL);") {
			exec_line = NR; exec_count++
		}
		END {
			if (clear_count != 1 || commit_count != 1 ||
			    restore_count != 1 || environment_count != 1 ||
			    environment_seed_count != 1 ||
			    username_bound_count != 1 || username_count != 1 ||
			    setgid_count != 1 || initgroups_count != 1 ||
			    setuid_count != 1 || setreuid_count != 1 ||
			    root_restore_count != 1 || exec_count != 1)
				exit 1
			print environment_seed_line, username_bound_line,
			    username_line, clear_line, commit_line,
			    setgid_line, initgroups_line, restore_line,
			    environment_line, setuid_line, exec_line
		}
	' "$1") || return 1
	set -- $positions
	[ "$1" -lt "$2" ] && [ "$2" -lt "$3" ] &&
	    [ "$3" -lt "$4" ] && [ "$4" -lt "$5" ] &&
	    [ "$5" -lt "$6" ] && [ "$6" -lt "$7" ] &&
	    [ "$7" -lt "$8" ] && [ "$8" -lt "$9" ] &&
	    [ "$9" -lt "${10}" ] &&
	    [ "${10}" -lt "${11}" ]
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
missing_privilege_check_fixture=$temporary_directory/missing-privilege-check.c
missing_kerberos_check_fixture=$temporary_directory/missing-kerberos-check.c

printf '%s\n' \
	'char *domain, *salt, *envinit[1] = { NULL }, *ttyn, *pp;' \
	'if (strlen(*argv) > UT_NAMESIZE)' \
	'username = *argv;' \
	'login_local_modes_clear(0, &saved_local_modes);' \
	'(void)alarm((u_int)0);' \
	'if (setgid(pwd->pw_gid) < 0)' \
	'if (initgroups(username, pwd->pw_gid) < 0)' \
	'if (setreuid(geteuid(), pwd->pw_uid) < 0)' \
	'if (setuid(0) < 0)' \
	'login_local_modes_restore(0, saved_local_modes);' \
	'if (!pflag)' \
	'if (setuid(pwd->pw_uid) < 0)' \
	'execlp(pwd->pw_shell, tbuf, (char *)NULL);' >"$good_fixture"
awk '$0 != "login_local_modes_restore(0, saved_local_modes);"' \
	"$good_fixture" >"$missing_fixture"
awk '
	$0 == "login_local_modes_clear(0, &saved_local_modes);" {
		clear_line = $0
		next
	}
	$0 == "(void)alarm((u_int)0);" {
		print
		print clear_line
		next
	}
	{ print }
' "$good_fixture" >"$reversed_fixture"
sed 's/if (setuid(pwd->pw_uid) < 0)/setuid(pwd->pw_uid);/' \
	"$good_fixture" >"$missing_privilege_check_fixture"
sed -e 's/if (setreuid(geteuid(), pwd->pw_uid) < 0)/setreuid(geteuid(), pwd->pw_uid);/' \
	-e 's/if (setuid(0) < 0)/setuid(0);/' \
	"$good_fixture" >"$missing_kerberos_check_fixture"

check_handoff_order "$good_fixture"
if check_handoff_order "$missing_fixture" ||
	check_handoff_order "$reversed_fixture" ||
	check_handoff_order "$missing_privilege_check_fixture" ||
	check_handoff_order "$missing_kerberos_check_fixture"; then
	echo "login handoff: negative control passed" >&2
	exit 1
fi

echo "login handoff source order: pass"
