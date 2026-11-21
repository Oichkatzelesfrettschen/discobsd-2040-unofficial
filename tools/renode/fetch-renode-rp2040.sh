#!/bin/sh
# Fetch and build the third-party Renode RP2040 peripheral models.
#
# Renode's own distribution (renode-bin 1.17.0, from the AUR) ships no
# RP2040 or Raspberry Pi Pico platform: 'find /opt/renode -iname "*rp2040*"'
# turns up nothing but litex_picorv32 and picosoc. The RP2040 model used by
# this port comes from a separate project, matgla/Renode_RP2040, pinned by
# commit so the build is reproducible. It is not vendored into this tree:
# its peripherals/ and bootroms/ directories carry their own licenses (MIT
# for the C# peripheral models, the upstream pico-bootrom-rp2040 license for
# the boot ROM ELF) and its emulation/ directory is a multi-hundred-file C#
# project, better fetched than duplicated.
#
# The upstream project targets netstandard2.1 in its csproj, which predates
# the .NET the Renode 1.17.0 assemblies are built against: building
# unmodified fails with
#   CSC : error CS1705: Assembly 'Infrastructure' ... uses 'System.Runtime,
#   Version=8.0.0.0' which has a higher version than referenced assembly
#   'System.Runtime' with identity 'System.Runtime, Version=4.1.2.0'
# Retargeting emulation/Peripherals.csproj to net8.0, the framework
# Renode.runtimeconfig.json names (with rollForward Major), and
# load_peripherals.py to look under bin/Release/net8.0, resolves this without
# touching any peripheral model. The models must not target a newer framework
# than Renode runs on: a host with the .NET 8 runtime runs Renode on 8, which
# cannot load a net9.0 assembly, while a host whose oldest runtime is newer
# rolls Renode and the net8.0 models forward together.
#
# cores/rp2040.repl applies Renode's RP2040 register description (an SVD
# file) to the system bus by URL, which makes every emulator start depend on
# that server. The fetch below downloads the archive once, requires its
# SHA-256, unpacks it beside the platform file and rewrites the URL to that
# local path. The archive is kept beside dest-dir, where a later run or a CI
# cache finds it and skips the download.
#
# Usage: sh fetch-renode-rp2040.sh [dest-dir]
# Default dest-dir is tools/renode/vendor/Renode_RP2040, ignored by git.
# RENODE_SVD_ARCHIVE names the kept archive (default: RP2040.svd.gz beside
# dest-dir).

set -eu

RP2040_REPO=https://github.com/matgla/Renode_RP2040.git
RP2040_COMMIT=205a5e4b25440582008a4292074bb07f80a72328

RP2040_SVD_URL=https://dl.antmicro.com/projects/renode/svd/RP2040.svd.gz
RP2040_SVD_SHA256=7ab4407a73275cf37099e3e451a7d393f25513ee819ca2c48fe546dac38cc4e1

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-"$SCRIPT_DIR/vendor/Renode_RP2040"}
SVD_ARCHIVE=${RENODE_SVD_ARCHIVE:-"$(dirname "$DEST")/RP2040.svd.gz"}

svd_archive_matches() {
	[ -r "$SVD_ARCHIVE" ] &&
		printf '%s  %s\n' "$RP2040_SVD_SHA256" "$SVD_ARCHIVE" |
		sha256sum --check --status
}

# --force because the patches below overlap: once 0002 has rewritten lines
# 0001 introduced, neither one applies to nor reverses out of the resulting
# tree, and a second fetch would stop on a patch that is in fact applied.
# Discarding tracked edits returns the tree to the pin every time, so the
# patch series is always applied to the revision it was written against.
# This directory is a fetched vendor tree; work meant to survive belongs in
# patches/ rather than in an edit here.
if [ -d "$DEST/.git" ]; then
	git -C "$DEST" fetch --depth 1 origin "$RP2040_COMMIT"
	git -C "$DEST" checkout --force "$RP2040_COMMIT"
else
	rm -rf "$DEST"
	git clone "$RP2040_REPO" "$DEST"
	git -C "$DEST" checkout "$RP2040_COMMIT"
fi

sed -i 's#<TargetFramework>netstandard2\.1</TargetFramework>#<TargetFramework>net8.0</TargetFramework>#' \
	"$DEST/emulation/Peripherals.csproj"
sed -i 's#"netstandard2\.1"#"net8.0"#' "$DEST/cores/load_peripherals.py"

# Corrections this port carries against the pinned revision, applied in
# filename order because 0002 rewrites lines 0001 introduces. Each is
# checked before it is applied, so a patch that no longer matches stops the
# fetch rather than leaving a half-corrected tree that builds and
# misbehaves. The already-applied case survives for a tree somebody patched
# by hand; the forced checkout above means the ordinary run never hits it.
# sys/arch/rp2040/doc/research/emulation.md records what each one fixes and
# how it was measured.
for patch in "$SCRIPT_DIR"/patches/*.patch; do
	[ -e "$patch" ] || break
	name=$(basename "$patch")
	if git -C "$DEST" apply --reverse --check "$patch" 2>/dev/null; then
		echo "$name: already applied"
		continue
	fi
	git -C "$DEST" apply --check "$patch" || {
		echo "$name does not apply to $RP2040_COMMIT; the pin and the" >&2
		echo "patch have to move together." >&2
		exit 1
	}
	git -C "$DEST" apply "$patch"
	echo "$name: applied"
done

if svd_archive_matches; then
	echo "RP2040.svd.gz: kept archive matches $RP2040_SVD_SHA256"
else
	mkdir -p "$(dirname "$SVD_ARCHIVE")"
	curl --fail --location --silent --show-error --retry 5 \
		--output "$SVD_ARCHIVE.part" "$RP2040_SVD_URL"
	mv "$SVD_ARCHIVE.part" "$SVD_ARCHIVE"
	svd_archive_matches || {
		echo "RP2040.svd.gz from $RP2040_SVD_URL is not" >&2
		echo "$RP2040_SVD_SHA256; the pin and the file have to move together." >&2
		exit 1
	}
	echo "RP2040.svd.gz: fetched"
fi
SVD_FILE=$(cd "$DEST/cores" && pwd)/RP2040.svd
gunzip -c "$SVD_ARCHIVE" > "$SVD_FILE"
# The path lands in a sed replacement and in a Renode monitor command, so it
# is held to characters neither one interprets.
case $SVD_FILE in
*[!A-Za-z0-9_./+-]*)
	echo "$SVD_FILE: the models' path may hold only letters, digits and _ . / + -" >&2
	exit 1
	;;
esac
sed -i "s#ApplySVD @$RP2040_SVD_URL\$#ApplySVD @$SVD_FILE#" "$DEST/cores/rp2040.repl"
grep -q "ApplySVD @$SVD_FILE\$" "$DEST/cores/rp2040.repl" || {
	echo "cores/rp2040.repl at $RP2040_COMMIT has no ApplySVD line for" >&2
	echo "$RP2040_SVD_URL; the pin and this rewrite have to move together." >&2
	exit 1
}
if grep -q 'https\{0,1\}://' "$DEST/cores/rp2040.repl"; then
	echo "cores/rp2040.repl still names a URL an emulator start would fetch" >&2
	exit 1
fi

dotnet build "$DEST/emulation/Peripherals.csproj" -c Release

# The gates compare this stamp with the series in patches/ before they boot
# the models (models-series.sh).
sh "$SCRIPT_DIR/models-series.sh" stamp "$DEST/emulation/bin/Release/net8.0/Peripherals.dll"

echo "built: $DEST/emulation/bin/Release/net8.0/Peripherals.dll"
