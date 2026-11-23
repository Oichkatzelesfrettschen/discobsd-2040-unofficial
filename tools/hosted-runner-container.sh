#!/bin/sh
# Run root Makefile targets inside an ubuntu:24.04 container that carries the
# packages the Linux job of .github/workflows/firmware.yml installs, so a gate
# that passes under the developer's toolchain meets the hosted runner's GCC,
# newlib and Python before a nightly dispatch does.
#
# usage: hosted-runner-container.sh [--rev REV] [--skip-build] [--rebuild]
#                                   [--keep] [--] TARGET...
#
# Package list: the apt and pipx lists are read from the workflow at run time
# (the "apt-get install" continuation block and every "pipx install" line of
# the step named "Install the host toolchain"). The image tag carries a hash of
# those lists, the engine's base image reference and EXTRA_APT_PACKAGES, so an
# edit to the workflow selects a new image and the two cannot drift apart.
# EXTRA_APT_PACKAGES lists what the hosted image carries outside that step:
# python3-setuptools gives python3-unicorn its distutils import, and
# groff-base gives games/cribbage the nroff its build runs.
#
# Tree: the container works on a private copy and the host tree is never
# written. The copy is a git repository whose object store borrows the host's
# (read-only alternates), holding the commit REV (default HEAD) as its checked
# out tree, the host's refs/remotes/origin refs, and the tracked changes of the
# host working tree (`git diff --binary HEAD`, staged and unstaged) applied on
# top when REV is not given. Untracked files are not copied. Gates that read a
# baseline with `git show` resolve any commit the host repository holds.
#
# Sequence: `bmake -jN MACHINE=rp2040 build`, as the workflow's first step,
# then `bmake MACHINE=rp2040 TARGET...` with PYTHON set to python3. The log
# streams to .build-logs/hosted-runner-<stamp>.XXXXXX under the host repository
# and the exit status is the gate's. picotool is not installed, so targets
# that convert UF2 images (flash, check-uf2-images) are out of scope. The
# workflow's per-step deadlines are not applied.
#
# exit: the status of the build or of the targets; 2 for a usage error or an
# engine that is absent ("not run").
set -eu

me=hosted-runner-container
EXTRA_APT_PACKAGES="python3-setuptools ca-certificates groff-base"
BASE_IMAGE=ubuntu:24.04
STEP_NAME="Install the host toolchain"

die() {
	printf '%s: %s\n' "$me" "$*" >&2
	exit 2
}

rev=
skip_build=no
rebuild=no
keep=no
while [ $# -gt 0 ]; do
	case $1 in
	--rev)
		[ $# -ge 2 ] || die "--rev needs a revision"
		rev=$2
		shift 2
		;;
	--skip-build) skip_build=yes; shift ;;
	--rebuild) rebuild=yes; shift ;;
	--keep) keep=yes; shift ;;
	-h|--help)
		sed -n '2,/^set -eu/p' "$0" | sed '$d; s/^# \{0,1\}//'
		exit 0
		;;
	--) shift; break ;;
	-*) die "unknown option $1" ;;
	*) break ;;
	esac
done
[ $# -gt 0 ] || die "no target; usage: $0 [--rev REV] TARGET..."

if command -v podman >/dev/null 2>&1; then
	engine=podman
elif command -v docker >/dev/null 2>&1; then
	engine=docker
else
	printf '%s: not run: neither podman nor docker is installed;' "$me" >&2
	printf ' install the podman package\n' >&2
	exit 2
fi

top=$(git rev-parse --show-toplevel) || die "not inside a git work tree"
cd "$top"
workflow=.github/workflows/firmware.yml
[ -f "$workflow" ] || die "$workflow is missing"
common=$(git rev-parse --path-format=absolute --git-common-dir)
objects=$common/objects
[ -d "$objects" ] || die "no object store at the git common directory"

# Package lists from the workflow step. awk prints the step's run block; the
# apt tokens follow "install" up to the first line without a continuation.
step=$(awk -v name="$STEP_NAME" '
	$0 ~ "- name: " name { on = 1; next }
	on && /^      - / { exit }
	on { print }
' "$workflow")
[ -n "$step" ] || die "step \"$STEP_NAME\" not found in $workflow"
apt_list=$(printf '%s\n' "$step" | awk '
	/apt-get install/ { on = 1; seen = 0 }
	on {
		line = $0
		cont = (line ~ /\\[ \t]*$/)
		sub(/\\[ \t]*$/, "", line)
		n = split(line, w, /[ \t]+/)
		for (i = 1; i <= n; i++) {
			if (w[i] == "install") { seen = 1; continue }
			if (seen && w[i] != "" && w[i] !~ /^-/) print w[i]
		}
		if (!cont) on = 0
	}
')
pipx_list=$(printf '%s\n' "$step" | awk '
	/pipx install/ { for (i = 1; i <= NF; i++) if ($i == "install") {
		for (j = i + 1; j <= NF; j++) if ($j !~ /^-/) print $j } }
')
[ -n "$apt_list" ] || die "no apt packages found in $workflow"
for p in $apt_list $pipx_list $EXTRA_APT_PACKAGES; do
	case $p in
	''|*[!a-z0-9.+-]*|-*) die "package name \"$p\" is outside [a-z0-9.+-]" ;;
	esac
done
apt_all=$(printf '%s\n' $apt_list $EXTRA_APT_PACKAGES | sort -u | tr '\n' ' ')
apt_all=${apt_all% }
pipx_all=$(printf '%s\n' $pipx_list | sort -u | tr '\n' ' ')
pipx_all=${pipx_all% }

sum_tool=
for t in sha256sum shasum; do
	if command -v "$t" >/dev/null 2>&1; then
		sum_tool=$t
		break
	fi
done
[ -n "$sum_tool" ] || die "sha256sum or shasum is required"
case $sum_tool in
shasum) sum_tool="shasum -a 256" ;;
esac
# The ISO week is part of the tag: package names stay the same while the
# archive and the hosted runner move to newer versions, so an image is
# rebuilt at least weekly without --rebuild.
hash=$(printf '%s|%s|%s|%s|%s\n' "$BASE_IMAGE" "$apt_all" "$pipx_all" \
	"$engine" "$(date -u +%G-%V)" | $sum_tool | cut -c1-12)
image=localhost/gizmobsd-hosted-runner:$hash

work=$(mktemp -d "${TMPDIR:-/tmp}/$me.XXXXXX")
cleanup() {
	if [ "$keep" = yes ]; then
		printf '%s: kept %s\n' "$me" "$work" >&2
		return 0
	fi
	if [ -d "$work" ]; then
		chmod -R u+w "$work" 2>/dev/null || true
		if command -v trash-put >/dev/null 2>&1; then
			trash-put "$work" 2>/dev/null || true
		else
			rm -r "$work" 2>/dev/null || true
		fi
	fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

have_image=no
"$engine" image inspect "$image" >/dev/null 2>&1 && have_image=yes
if [ "$rebuild" = yes ] || [ "$have_image" = no ]; then
	ctx=$work/image
	mkdir -p "$ctx"
	cat >"$ctx/Containerfile" <<EOF
FROM $BASE_IMAGE
ARG APT_PACKAGES
ARG PIPX_PACKAGES
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \\
 && apt-get install -y --no-install-recommends \$APT_PACKAGES \\
 && rm -rf /var/lib/apt/lists/*
ENV PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin
RUN for p in \$PIPX_PACKAGES; do pipx install "\$p"; done
EOF
	printf '%s: building %s (%s packages)\n' "$me" "$image" \
		"$(printf '%s\n' $apt_all | wc -l | tr -d ' ')" >&2
	# A build happens only for a new tag or --rebuild, and both mean the
	# packages are to be fetched again: the engine's layer cache would
	# otherwise replay an old apt or pipx install under the new tag.
	"$engine" build --pull --no-cache -t "$image" \
		--build-arg "APT_PACKAGES=$apt_all" \
		--build-arg "PIPX_PACKAGES=$pipx_all" \
		-f "$ctx/Containerfile" "$ctx" >&2
fi

# The private tree.
tree=$work/tree
mkdir -p "$tree"
want=${rev:-HEAD}
commit=$(git rev-parse --verify --end-of-options "$want^{commit}") ||
	die "revision \"$want\" does not name a commit"
git -C "$tree" init -q
# Every object store the host repository reads: its own and, transitively,
# the ones its info/alternates name (a --shared or --reference clone keeps
# most objects there). The private tree borrows all of them and each is
# mounted read-only at its own path.
stores=$work/stores
(cd "$objects" && pwd -P) >"$stores"
i=1
while :; do
	d=$(sed -n "${i}p" "$stores")
	[ -n "$d" ] || break
	if [ -f "$d/info/alternates" ]; then
		while IFS= read -r a; do
			case $a in
			'' | '#'*) continue ;;
			/*) ;;
			*) a=$d/$a ;;
			esac
			r=$(cd "$a" 2>/dev/null && pwd -P) || continue
			grep -qxF "$r" "$stores" || printf '%s\n' "$r" >>"$stores"
		done <"$d/info/alternates"
	fi
	i=$((i + 1))
done
store_mounts=
while IFS= read -r d; do
	case $d in
	*[!A-Za-z0-9_./+-]*) die "object store path has a character a mount option cannot carry: $d" ;;
	esac
	store_mounts="$store_mounts -v $d:$d:ro"
done <"$stores"
cp "$stores" "$tree/.git/objects/info/alternates"
git -C "$tree" update-ref refs/heads/main "$commit"
git -C "$tree" symbolic-ref HEAD refs/heads/main
git for-each-ref --format='%(objectname) %(refname)' refs/remotes/origin |
	while read -r sha ref; do
		git -C "$tree" update-ref "$ref" "$sha"
	done
git -C "$tree" read-tree -u --reset "$commit"
if [ -z "$rev" ]; then
	if ! git diff --quiet HEAD; then
		git diff --binary HEAD | git -C "$tree" apply --index
		printf '%s: applied tracked changes of the host tree\n' "$me" >&2
	fi
fi

mkdir -p .build-logs
stamp=$(date +%Y%m%d-%H%M%S)
log=$(mktemp ".build-logs/hosted-runner-$stamp.XXXXXX")
status_file=$work/status
printf '%s: %s on %s, log %s\n' "$me" "$*" "$(printf '%.12s' "$commit")" \
	"$log" >&2

case $engine in
podman) user_args="--userns=keep-id" ;;
*) user_args="--user $(id -u):$(id -g)" ;;
esac

# shellcheck disable=SC2016
inner='set -eu
skip=$1
shift
PYTHON=$(command -v python3)
export PYTHON
if [ "$skip" = no ]; then
	bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 build
fi
exec bmake MACHINE=rp2040 "$@"'

start=$(date +%s)
{
	set +e
	# $user_args holds fixed option words only; $store_mounts holds paths
	# checked against an allow-list above.
	# shellcheck disable=SC2086
	"$engine" run --rm $user_args \
		--security-opt label=disable \
		-e HOME=/tmp/home -e TERM=dumb \
		-e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory \
		-e 'GIT_CONFIG_VALUE_0=*' \
		-v "$tree:/work" $store_mounts \
		-w /work "$image" \
		sh -c "mkdir -p /tmp/home && $inner" sh "$skip_build" "$@" 2>&1
	echo $? >"$status_file"
} | tee "$log"
rc=$(cat "$status_file" 2>/dev/null || echo 1)
elapsed=$(($(date +%s) - start))
if [ "$rc" -eq 0 ]; then
	verdict=pass
else
	verdict="fail ($rc)"
fi
printf '%s: %s: %s in %ss, log %s\n' "$me" "$*" "$verdict" "$elapsed" \
	"$log" | tee -a "$log" >&2
exit "$rc"
