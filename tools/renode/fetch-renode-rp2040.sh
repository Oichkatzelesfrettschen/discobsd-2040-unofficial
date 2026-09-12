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

if [ -d "$DEST/.git" ]; then
	git -C "$DEST" fetch --depth 1 origin "$RP2040_COMMIT"
	git -C "$DEST" checkout "$RP2040_COMMIT"
else
	rm -rf "$DEST"
	git clone "$RP2040_REPO" "$DEST"
	git -C "$DEST" checkout "$RP2040_COMMIT"
fi

sed -i 's#<TargetFramework>netstandard2\.1</TargetFramework>#<TargetFramework>net9.0</TargetFramework>#' \
	"$DEST/emulation/Peripherals.csproj"
sed -i 's#"netstandard2\.1"#"net9.0"#' "$DEST/cores/load_peripherals.py"

dotnet build "$DEST/emulation/Peripherals.csproj" -c Release

echo "built: $DEST/emulation/bin/Release/net9.0/Peripherals.dll"
