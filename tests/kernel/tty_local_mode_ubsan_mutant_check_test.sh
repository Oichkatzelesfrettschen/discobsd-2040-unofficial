#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 CHECKER" >&2
	exit 2
fi

checker=$1
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/tty-local-mode-ubsan.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

cat >"$temporary_directory/rejected" <<'EOF'
#!/bin/sh
echo "runtime error: left shift of 32768 by 16 places" >&2
exit 1
EOF
cat >"$temporary_directory/success" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$temporary_directory/wrong-diagnostic" <<'EOF'
#!/bin/sh
echo "runtime error: unrelated" >&2
exit 1
EOF
cat >"$temporary_directory/wrong-status" <<'EOF'
#!/bin/sh
echo "runtime error: left shift of 32768 by 16 places" >&2
exit 2
EOF
chmod +x "$temporary_directory/rejected" "$temporary_directory/success" \
    "$temporary_directory/wrong-diagnostic" "$temporary_directory/wrong-status"

(
	cd "$temporary_directory"
	sh "$checker" ./rejected >/dev/null
	if sh "$checker" ./success >/dev/null 2>&1; then
		echo "FAIL tty local-mode UBSan oracle accepted success" >&2
		exit 1
	fi
	if sh "$checker" ./wrong-diagnostic >/dev/null 2>&1; then
		echo "FAIL tty local-mode UBSan oracle accepted wrong diagnostic" >&2
		exit 1
	fi
	if sh "$checker" ./wrong-status >/dev/null 2>&1; then
		echo "FAIL tty local-mode UBSan oracle accepted wrong status" >&2
		exit 1
	fi
)

echo "PASS tty local-mode UBSan mutation oracle calibrated"
