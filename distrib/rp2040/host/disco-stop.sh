#!/bin/sh
#
# disco-stop.sh -- take the DiscoBSD web console down: stops the systemd
# user units when installed, otherwise the detached servers disco-go.sh
# started (pid files under ~/.config/discobsd).
set -eu
conf=${XDG_CONFIG_HOME:-$HOME/.config}/discobsd
if [ -r "$conf/../systemd/user/discobsd-web.service" ] && command -v systemctl >/dev/null 2>&1; then
	systemctl --user stop discobsd-link.service discobsd-web.service
	echo "DiscoBSD console stopped (systemd units)."
else
	stopped=0
	for n in link web; do
		if [ -r "$conf/$n.pid" ]; then
			pid=$(cat "$conf/$n.pid")
			if kill -0 "$pid" 2>/dev/null; then
				kill "$pid" && stopped=$((stopped + 1))
			fi
			rm -f "$conf/$n.pid"
		fi
	done
	echo "DiscoBSD console stopped ($stopped process(es))."
fi
