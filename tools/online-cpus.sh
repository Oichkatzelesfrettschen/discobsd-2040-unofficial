#!/bin/sh
# Print the CPU count accepted by make's -j option: the online CPUs, bounded
# on Linux by the process's affinity mask (Cpus_allowed_list in
# /proc/self/status) and by the CPU quota of its cgroup or any ancestor
# (cgroup v2 cpu.max, cgroup v1 cpu.cfs_quota_us over cpu.cfs_period_us),
# rounded up. getconf covers the CI runners; sysctl covers BSD hosts and nproc
# covers GNU hosts whose getconf omits the extension. A host without any probe
# stays usable. tools/available_cpus.py applies the same bounds in Python, and
# tools/available_cpus_test.py holds both to fixture hierarchies through
# ONLINE_CPUS_PROC_SELF and ONLINE_CPUS_CGROUP_ROOT, which default to
# /proc/self and /sys/fs/cgroup, and ONLINE_CPUS_ONLINE, which replaces the
# probed online count so a fixture's verdict does not depend on the host.
set -eu

proc_self=${ONLINE_CPUS_PROC_SELF:-/proc/self}
cgroup_root=${ONLINE_CPUS_CGROUP_ROOT:-/sys/fs/cgroup}

valid_count() {
	case $1 in
	''|*[!0-9]*|0) return 1 ;;
	esac
	return 0
}

online_count=${ONLINE_CPUS_ONLINE:-}
if ! valid_count "$online_count" && command -v getconf >/dev/null 2>&1; then
	online_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null || :)
fi
if ! valid_count "$online_count" && command -v sysctl >/dev/null 2>&1; then
	online_count=$(sysctl -n hw.logicalcpu 2>/dev/null ||
	    sysctl -n hw.ncpu 2>/dev/null || :)
fi
if ! valid_count "$online_count" && command -v nproc >/dev/null 2>&1; then
	online_count=$(nproc 2>/dev/null || :)
fi
if ! valid_count "$online_count"; then
	online_count=1
fi

# Cpus_allowed_list reads like "0-3,8,10-11"; each range counts its span.
if [ -r "$proc_self/status" ]; then
	affinity=$(awk -F'\t' '$1 == "Cpus_allowed_list:" {
		n = 0; k = split($2, part, ",")
		for (i = 1; i <= k; i++) {
			if (split(part[i], span, "-") == 2) n += span[2] - span[1] + 1
			else if (part[i] != "") n++
		}
		print n
	}' "$proc_self/status" 2>/dev/null || :)
	if valid_count "$affinity" && [ "$affinity" -lt "$online_count" ]; then
		online_count=$affinity
	fi
fi

# Print every quota on the walk from the process's cgroup to the root, as
# CPUs rounded up; an ancestor's quota binds its descendants.
cgroup_quotas() {
	while IFS=: read -r hierarchy controllers path; do
		if [ "$hierarchy" = 0 ] && [ -z "$controllers" ]; then
			mount=$cgroup_root
			kind=v2
		else
			case ",$controllers," in
			*,cpu,*) ;;
			*) continue ;;
			esac
			kind=v1
			mount=
		fi
		cgroup_walk
	done
}

# Walk path from the process's cgroup to the root under each mount that
# holds the hierarchy: the v2 root, or every name a v1 cpu controller is
# mounted under, so an empty sibling directory hides nothing.
cgroup_walk() {
	if [ "$kind" = v2 ]; then
		set -- "$mount"
	else
		set -- "$cgroup_root/cpu" "$cgroup_root/cpu,cpuacct" \
		    "$cgroup_root/cpuacct,cpu"
	fi
	for mount in "$@"; do
		[ -d "$mount" ] || continue
		node=${path%/}
		while :; do
			dir=$mount$node
			if [ "$kind" = v2 ] && [ -r "$dir/cpu.max" ]; then
				awk 'NR == 1 && NF == 2 && $1 ~ /^[0-9]+$/ &&
				    $2 ~ /^[0-9]+$/ && $1 > 0 && $2 > 0 {
					q = $1 / $2; c = int(q); if (c < q) c++; print c
				}' "$dir/cpu.max" 2>/dev/null || :
			elif [ "$kind" = v1 ] && [ -r "$dir/cpu.cfs_quota_us" ] &&
			    [ -r "$dir/cpu.cfs_period_us" ]; then
				awk 'FNR == 1 && NR == 1 { q = $0; next }
				    FNR == 1 && q ~ /^[0-9]+$/ && $0 ~ /^[0-9]+$/ &&
				    q > 0 && $0 > 0 {
					r = q / $0; c = int(r); if (c < r) c++; print c
				}' "$dir/cpu.cfs_quota_us" "$dir/cpu.cfs_period_us" \
				    2>/dev/null || :
			fi
			[ -n "$node" ] || break
			node=${node%/*}
		done
	done
}

if [ -r "$proc_self/cgroup" ]; then
	for quota in $(cgroup_quotas < "$proc_self/cgroup"); do
		if valid_count "$quota" && [ "$quota" -lt "$online_count" ]; then
			online_count=$quota
		fi
	done
fi

printf '%s\n' "$online_count"
