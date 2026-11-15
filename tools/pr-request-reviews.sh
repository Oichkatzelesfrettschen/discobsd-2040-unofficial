#!/bin/sh
# usage: pr-request-reviews.sh [BRANCH]
#
# Request the hosted Codex and Copilot reviews on the open pull request
# whose head is BRANCH (default: the current branch) in the origin
# repository. The number is resolved from the branch, never typed: several
# sessions open pull requests concurrently, so the number after the last
# one seen can belong to someone else. Stops unless exactly one open pull
# request has that head.
set -eu

die() {
	echo "pr-request-reviews: $*" >&2
	exit 1
}

command -v gh >/dev/null 2>&1 || die "not run: install the GitHub CLI (gh)"
branch=${1:-$(git rev-parse --abbrev-ref HEAD)}
[ "$branch" != HEAD ] || die "detached HEAD: name the branch"
url=$(git remote get-url origin)
repo=$(printf '%s\n' "$url" | sed -E 's#^(https://github\.com/|git@github\.com:)##; s#\.git$##')
case "$repo" in
*/*) ;;
*) die "origin is not a GitHub repository: $url" ;;
esac
# GitHub owner names compare without case.
owner=$(printf '%s\n' "${repo%%/*}" | tr '[:upper:]' '[:lower:]')
numbers=$(gh pr list --repo "$repo" --state open --head "$branch" \
    --json number,headRepositoryOwner \
    --jq ".[] | select((.headRepositoryOwner.login | ascii_downcase) == \"$owner\") | .number")
count=$(printf '%s\n' "$numbers" | grep -c . || true)
[ "$count" -eq 1 ] || die "$count open pull requests in $repo have head $branch"
number=$numbers
head=$(gh pr view "$number" --repo "$repo" --json headRefName --jq .headRefName)
[ "$head" = "$branch" ] || die "pull request $number has head $head, not $branch"
gh pr comment "$number" --repo "$repo" --body "@codex review" >/dev/null
gh pr edit "$number" --repo "$repo" --add-reviewer copilot-pull-request-reviewer >/dev/null
echo "pr-request-reviews: requested Codex and Copilot on $repo#$number ($branch)"
