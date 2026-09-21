#!/bin/sh
# Verify the finite legacy move map before or after archival relocation.

set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: $0 SOURCE_ROOT MAP_PATH source|archive" >&2
	exit 2
fi

source_root=$1
map_path=$2
verification_mode=$3
boundary_path=$source_root/tools/architecture-isolation/legacy-boundary-files.list

case $verification_mode in
source|archive)
	;;
*)
	echo "$0: mode must be source or archive" >&2
	exit 2
	;;
esac

temporary_parent=${TMPDIR:-/tmp}
if [ ! -d "$temporary_parent" ]; then
	echo "$temporary_parent: temporary parent is not a directory" >&2
	exit 2
fi
temporary_parent=$(CDPATH= cd "$temporary_parent" && pwd -P)
temporary_directory=$(mktemp -d "$temporary_parent/discobsd-legacy-map-verify.XXXXXX")
rows_path=$temporary_directory/rows.tsv
sources_path=$temporary_directory/sources
destinations_path=$temporary_directory/destinations
declared_archive_path=$temporary_directory/declared-archive
tracked_archive_path=$temporary_directory/tracked-archive

cleanup_temporary_directory()
{
	case $temporary_directory in
	"$temporary_parent"/discobsd-legacy-map-verify.*)
		find "$temporary_directory" -depth \
		    \( -type f -o -type l \) -exec unlink {} \; 2>/dev/null || :
		find "$temporary_directory" -depth -type d \
		    -exec rmdir {} \; 2>/dev/null || :
		;;
	*)
		echo "legacy map verifier: refusing temporary cleanup outside $temporary_parent" >&2
		;;
	esac
}

trap cleanup_temporary_directory EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

awk -F '\t' '
    /^#/ { next }
    $1 == "class" { next }
    NF != 6 { print FILENAME ":" NR ": expected six tab-separated fields" > "/dev/stderr"; failed=1 }
    NF == 6 { print }
    END { exit failed }
' "$map_path" > "$rows_path"

source_revision=$(awk -F '\t' '$1 == "# source-revision" { print $2; count++ } END { if (count != 1) exit 1 }' "$map_path") || {
	echo "$map_path: expected one source-revision record" >&2
	exit 1
}
git -C "$source_root" rev-parse --verify "$source_revision^{commit}" >/dev/null

cut -f2 "$rows_path" > "$sources_path"
cut -f3 "$rows_path" > "$destinations_path"
if [ "$(LC_ALL=C sort "$sources_path" | uniq -d | wc -l)" -ne 0 ]; then
	echo "$map_path: duplicate source path" >&2
	exit 1
fi
if [ "$(LC_ALL=C sort "$destinations_path" | uniq -d | wc -l)" -ne 0 ]; then
	echo "$map_path: duplicate destination path" >&2
	exit 1
fi

non_arm_count=$(awk -F '\t' '$1 == "legacy-non-arm" { count++ } END { print count + 0 }' "$rows_path")
pdp11_count=$(awk -F '\t' '$1 == "legacy-pdp11-v6" { count++ } END { print count + 0 }' "$rows_path")
unsupported_platform_count=$(awk -F '\t' '$1 == "legacy-unsupported-platform" { count++ } END { print count + 0 }' "$rows_path")
row_count=$(wc -l < "$rows_path")
if [ "$non_arm_count" -ne 1008 ] || [ "$pdp11_count" -ne 22 ] || \
    [ "$unsupported_platform_count" -ne 2 ] || [ "$row_count" -ne 1032 ]; then
	echo "$map_path: counts are rows=$row_count non-arm=$non_arm_count pdp11-v6=$pdp11_count unsupported-platform=$unsupported_platform_count; expected 1032, 1008, 22, and 2" >&2
	exit 1
fi

while IFS="$(printf '\t')" read -r class_name source_path destination_path \
    origin_blob origin_sha256 archive_policy
do
	case $class_name in
	legacy-non-arm|legacy-pdp11-v6|legacy-unsupported-platform)
		;;
	*)
		echo "$map_path: unknown class $class_name" >&2
		exit 1
		;;
	esac
	case $archive_policy in
	immutable|maintained)
		;;
	*)
		echo "$source_path: unknown archive policy $archive_policy" >&2
		exit 1
		;;
	esac
	case $destination_path in
	legacy/non-arm/*)
		[ "$class_name" = legacy-non-arm ] || {
			echo "$destination_path: class mismatch" >&2
			exit 1
		}
		;;
	legacy/pdp11-v6/*)
		[ "$class_name" = legacy-pdp11-v6 ] || {
			echo "$destination_path: class mismatch" >&2
			exit 1
		}
		;;
	legacy/unsupported-platforms/*)
		[ "$class_name" = legacy-unsupported-platform ] || {
			echo "$destination_path: class mismatch" >&2
			exit 1
		}
		;;
	*)
		echo "$destination_path: destination leaves the declared legacy roots" >&2
		exit 1
		;;
	esac

	actual_origin_blob=$(git -C "$source_root" rev-parse "$source_revision:$source_path") || {
		echo "$source_revision: missing $source_path" >&2
		exit 1
	}
	if [ "$actual_origin_blob" != "$origin_blob" ]; then
		echo "$source_path: origin blob is $actual_origin_blob, expected $origin_blob" >&2
		exit 1
	fi
	actual_origin_sha256=$(git -C "$source_root" cat-file blob "$actual_origin_blob" | sha256sum | awk '{ print $1 }')
	if [ "$actual_origin_sha256" != "$origin_sha256" ]; then
		echo "$source_path: origin SHA-256 is $actual_origin_sha256, expected $origin_sha256" >&2
		exit 1
	fi

	if [ "$verification_mode" = archive ]; then
		if git -C "$source_root" ls-files --error-unmatch -- "$source_path" >/dev/null 2>&1; then
			echo "$source_path: source path remains tracked after archival move" >&2
			exit 1
		fi
		index_entry=$(git -C "$source_root" ls-files -s -- "$destination_path")
		if [ -z "$index_entry" ]; then
			echo "$destination_path: destination is not tracked" >&2
			exit 1
		fi
		destination_blob=$(printf '%s\n' "$index_entry" | awk 'NR == 1 { print $2 } END { if (NR != 1) exit 1 }') || {
			echo "$destination_path: destination has multiple index stages" >&2
			exit 1
		}
		if [ "$archive_policy" = immutable ] && \
		    [ "$destination_blob" != "$origin_blob" ]; then
			echo "$destination_path: destination blob is $destination_blob, expected source blob $origin_blob" >&2
			exit 1
		fi
		if ! git -C "$source_root" diff --quiet -- "$destination_path"; then
			echo "$destination_path: working tree differs from the indexed archive blob" >&2
			exit 1
		fi
		if [ "$archive_policy" = immutable ]; then
			actual_sha256=$(git -C "$source_root" cat-file blob "$destination_blob" | sha256sum | awk '{ print $1 }')
			if [ "$actual_sha256" != "$origin_sha256" ]; then
				echo "$destination_path: SHA-256 is $actual_sha256, expected $origin_sha256" >&2
				exit 1
			fi
		fi
	fi
done < "$rows_path"

if [ "$verification_mode" = archive ]; then
	if [ ! -f "$boundary_path" ]; then
		echo "$boundary_path: boundary file list is absent" >&2
		exit 1
	fi
	awk 'NF && $1 !~ /^#/ { print }' "$boundary_path" |
	    LC_ALL=C sort -u > "$declared_archive_path"
	cat "$destinations_path" >> "$declared_archive_path"
	LC_ALL=C sort -u -o "$declared_archive_path" "$declared_archive_path"
	git -C "$source_root" ls-files -- 'legacy/non-arm/**' \
	    'legacy/pdp11-v6/**' 'legacy/unsupported-platforms/**' |
	    LC_ALL=C sort > "$tracked_archive_path"
	if ! diff -u "$declared_archive_path" "$tracked_archive_path"; then
		echo "legacy archive paths differ from the move map and boundary list" >&2
		exit 1
	fi
fi

echo "legacy path map verified: mode=$verification_mode rows=$row_count non-arm=$non_arm_count pdp11-v6=$pdp11_count unsupported-platform=$unsupported_platform_count"
