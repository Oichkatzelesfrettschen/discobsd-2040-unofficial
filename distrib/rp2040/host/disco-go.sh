#!/bin/sh
#
# disco-go.sh -- bring the DiscoBSD web console up on the LAN from this
# directory: discobsd-web on port 7681 behind a token and discobsd-link on
# port 42069 redirecting to the tokenized URL. Runs both through the
# systemd user units when they are installed, otherwise detached from this
# shell with pid files under ~/.config/discobsd. Prints the URLs to hand out.
#
# Usage: ./disco-go.sh [--bind ADDR] [--port N] [--link-port N]
# Token: DISCOBSD_WEB_TOKEN in the environment or ~/.config/discobsd/web.env;
#        generated and saved there on first use.
set -eu
PYTHON=${PYTHON:-python3}
here=$(cd "$(dirname "$0")" && pwd)
conf=${XDG_CONFIG_HOME:-$HOME/.config}/discobsd
env="$conf/web.env"
bind=0.0.0.0; port=7681; lport=42069
while [ $# -gt 0 ]; do
	case $1 in
	--bind) bind=$2; shift 2 ;;
	--port) port=$2; shift 2 ;;
	--link-port) lport=$2; shift 2 ;;
	*) echo "usage: $0 [--bind ADDR] [--port N] [--link-port N]" >&2; exit 2 ;;
	esac
done
mkdir -p "$conf"
[ -r "$env" ] && . "$env"
token=${DISCOBSD_WEB_TOKEN:-}
if [ -z "$token" ]; then
	token=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n')
fi
ip=${DISCOBSD_HOST_IP:-}
if [ -z "$ip" ]; then
	ip=$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "src") print $(i + 1)}' | head -1)
fi
[ -n "$ip" ] || { echo "disco-go: cannot determine the host address" >&2; exit 1; }
umask 077
printf 'DISCOBSD_WEB_TOKEN=%s\nDISCOBSD_HOST_IP=%s\n' "$token" "$ip" > "$env"
umask 022
full="http://$ip:$port/?token=$token"
if [ -r "$conf/../systemd/user/discobsd-web.service" ] && command -v systemctl >/dev/null 2>&1; then
	systemctl --user daemon-reload
	systemctl --user restart discobsd-web.service discobsd-link.service
	mode=systemd
else
	for n in web link; do
		if [ -r "$conf/$n.pid" ] && kill -0 "$(cat "$conf/$n.pid")" 2>/dev/null; then
			kill "$(cat "$conf/$n.pid")"
		fi
	done
	nohup "$PYTHON" "$here/discobsd-web" --bind "$bind" --port "$port" --token "$token" > "$conf/web.log" 2>&1 &
	echo $! > "$conf/web.pid"
	nohup "$PYTHON" "$here/discobsd-link" --to "$full" --port "$lport" --bind "$bind" > "$conf/link.log" 2>&1 &
	echo $! > "$conf/link.pid"
	mode=detached
fi
sleep 1
short="http://$ip:$lport/"
if command -v curl >/dev/null 2>&1 && ! curl -s -m 3 -o /dev/null "$short"; then
	echo "disco-go: $short is not answering; see $conf/*.log or journalctl --user -u discobsd-link" >&2
	exit 1
fi
name=$(hostname 2>/dev/null | tr 'A-Z' 'a-z')
echo "DiscoBSD console is up ($mode):"
echo "  short:  $short"
[ -n "$name" ] && echo "          http://$name.local:$lport/"
echo "  full:   $full"
echo "Log in as operator; su for root. One browser session at a time."
