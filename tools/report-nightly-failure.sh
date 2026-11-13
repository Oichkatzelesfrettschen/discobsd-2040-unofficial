#!/bin/sh
# Reports the result of a scheduled firmware run to one GitHub issue.
#
#   sh tools/report-nightly-failure.sh               act on the current run
#   sh tools/report-nightly-failure.sh --failed FILE  print the failed jobs
#
# FILE holds the run's jobs as the GitHub REST API returns them
# (GET /repos/OWNER/REPO/actions/runs/ID/jobs; one or more JSON objects with
# a "jobs" array). Each finished job whose conclusion is anything but
# success, skipped or neutral (failure, timed_out, cancelled,
# startup_failure, action_required, ...) prints one line: its name, then
# the names of its steps that ended the same way. A run with no such job
# prints nothing.
#
# Without arguments the script reads GITHUB_REPOSITORY, GITHUB_RUN_ID,
# GITHUB_SHA and GITHUB_SERVER_URL, and calls gh with GH_TOKEN (issues:
# write, actions: read). A failed run opens the issue titled below, or adds
# a comment to the open one, naming the failed jobs and steps and the
# merges since the last scheduled run that passed. A passing run adds a
# comment to the open issue and closes it. tools/report_nightly_failure_test.sh
# holds the calibration cases for --failed.
set -eu

title="Nightly gates failed"

failed_jobs() {
	jq -r '
		def bad: . != null and . != "success" and . != "skipped" and . != "neutral";
		.jobs[]
		| select(.conclusion | bad)
		| "- " + .name + " (" + .conclusion + "): "
		  + ([.steps[]? | select(.conclusion | bad) | .name]
		     | if length == 0 then "no step recorded" else join("; ") end)
	' "$1"
}

case "${1-}" in
--failed)
	failed_jobs "${2:?jobs file required}"
	exit 0
	;;
'') ;;
*)
	echo "usage: report-nightly-failure.sh [--failed FILE]" >&2
	exit 2
	;;
esac

: "${GITHUB_REPOSITORY:?}" "${GITHUB_RUN_ID:?}" "${GITHUB_SHA:?}" "${GITHUB_SERVER_URL:?}"

work=$(mktemp -d)
trap 'rm -f "$work/jobs.json" "$work/body.md"; rmdir "$work"' EXIT

gh api --paginate \
	"repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/jobs?per_page=100" \
	>"$work/jobs.json"
failed=$(failed_jobs "$work/jobs.json")
run_url="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID"

issue=$(gh issue list --repo "$GITHUB_REPOSITORY" --state open \
	--search "\"$title\" in:title" --json number,title \
	--jq "[.[] | select(.title == \"$title\")][0].number // empty")

if [ -z "$failed" ]; then
	if [ -n "$issue" ]; then
		gh issue close "$issue" --repo "$GITHUB_REPOSITORY" \
			--comment "The scheduled run at $GITHUB_SHA passed: $run_url"
	fi
	exit 0
fi

previous=$(gh api \
	"repos/$GITHUB_REPOSITORY/actions/workflows/firmware.yml/runs?event=schedule&status=success&per_page=1" \
	--jq '.workflow_runs[0].head_sha // empty')
if [ "$previous" = "$GITHUB_SHA" ]; then
	merges=
	range="A scheduled run passed at this same commit, so no merge separates the two runs."
elif [ -n "$previous" ]; then
	merges=$(gh api --paginate \
		"repos/$GITHUB_REPOSITORY/compare/$previous...$GITHUB_SHA?per_page=100" \
		--jq '.commits[] | select((.parents | length) > 1)
		      | "- " + .sha[0:8] + " " + (.commit.message | split("\n")[0])')
	range="Merges since the last scheduled run that passed ($previous):"
else
	merges=
	range="No earlier scheduled run passed, so the range of merges is unknown."
fi

{
	printf 'The scheduled run at %s failed: %s\n\n' "$GITHUB_SHA" "$run_url"
	printf 'Failed jobs and steps:\n\n%s\n\n' "$failed"
	printf '%s\n\n%s\n' "$range" "$merges"
} >"$work/body.md"

if [ -n "$issue" ]; then
	gh issue comment "$issue" --repo "$GITHUB_REPOSITORY" \
		--body-file "$work/body.md"
else
	gh issue create --repo "$GITHUB_REPOSITORY" --title "$title" \
		--body-file "$work/body.md"
fi
