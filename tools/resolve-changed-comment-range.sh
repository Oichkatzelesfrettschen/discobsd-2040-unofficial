#!/bin/sh
# Resolve event revisions before the changed-comment gate reads either tree.
set -eu

if [ "$#" -ne 4 ]; then
	echo "usage: $0 root event-base event-head default-branch-ref" >&2
	exit 2
fi

root=$1
event_base=$2
event_head=$3
default_branch_ref=$4

resolve_commit() {
	git -C "$root" rev-parse --verify "$1^{commit}"
}

head_commit=$(resolve_commit "$event_head") || {
	echo "changed-comment range: cannot resolve event head $event_head" >&2
	exit 2
}

use_default_branch=false
case $event_base in
'') use_default_branch=true ;;
*[!0]*) ;;
*) use_default_branch=true ;;
esac

if [ "$use_default_branch" = true ]; then
	default_commit=$(resolve_commit "$default_branch_ref") || {
		echo "changed-comment range: cannot resolve default branch $default_branch_ref" >&2
		exit 2
	}
	event_base=$(git -C "$root" merge-base "$head_commit" "$default_commit") || {
		echo "changed-comment range: event head and default branch have no merge base" >&2
		exit 2
	}
fi

base_commit=$(resolve_commit "$event_base") || {
	echo "changed-comment range: cannot resolve event base $event_base" >&2
	exit 2
}

printf '%s %s\n' "$base_commit" "$head_commit"
