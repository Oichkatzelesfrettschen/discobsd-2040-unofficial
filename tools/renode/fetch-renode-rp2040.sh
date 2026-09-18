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
# the .NET the installed Renode build (1.17.0, .NET 9.0.19) and the system
# dotnet SDK actually ship: building unmodified fails with
#   CSC : error CS1705: Assembly 'Infrastructure' ... uses 'System.Runtime,
#   Version=8.0.0.0' which has a higher version than referenced assembly
#   'System.Runtime' with identity 'System.Runtime, Version=4.1.2.0'
# Retargeting emulation/Peripherals.csproj to net9.0, and load_peripherals.py
# to look under bin/Release/net9.0 instead of netstandard2.1, resolves this
# without touching any peripheral model.
#
# Usage: sh fetch-renode-rp2040.sh [dest-dir]
# Default dest-dir is tools/renode/vendor/Renode_RP2040, ignored by git.

set -eu

RP2040_REPO=https://github.com/matgla/Renode_RP2040.git
RP2040_COMMIT=205a5e4b25440582008a4292074bb07f80a72328

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-"$SCRIPT_DIR/vendor/Renode_RP2040"}

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

sed -i 's#<TargetFramework>netstandard2\.1</TargetFramework>#<TargetFramework>net9.0</TargetFramework>#' \
	"$DEST/emulation/Peripherals.csproj"
sed -i 's#"netstandard2\.1"#"net9.0"#' "$DEST/cores/load_peripherals.py"

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

dotnet build "$DEST/emulation/Peripherals.csproj" -c Release

echo "built: $DEST/emulation/bin/Release/net9.0/Peripherals.dll"
