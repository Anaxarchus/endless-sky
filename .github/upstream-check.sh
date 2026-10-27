#!/bin/bash
# Argosy (not upstream): test-merges upstream Endless Sky into the current
# checkout and runs the fork's checks on the result. Used by
# .github/workflows/upstream_check.yaml; see docs/upstream.md.
#
#   .github/upstream-check.sh <report-dir>
#
# Needs a clean git worktree. It runs `git merge --no-commit --no-ff` and never
# commits or pushes: afterwards the tree is left mid-merge (undo it with
# `git merge --abort`), so run it in CI or in a throwaway copy.
#
# Environment:
#   UPSTREAM_URL     default https://github.com/endless-sky/endless-sky.git
#   UPSTREAM_BRANCH  default master
#   UPSTREAM_REMOTE  the remote to fetch into (default upstream; added with
#                    UPSTREAM_URL if it does not exist)
#   NO_FETCH=1       use the remote-tracking branch as it is (no network)
#   SDL_INCLUDE      passed to tests/modloader/run.sh (default /usr/include)
#
# Writes to <report-dir>: status (ok or fail), merged (true or false),
# summary.md (the issue text), and a .log file per step. Exits 0 when the
# report was written, whatever it says; non-zero only if it could not run.
set -uo pipefail

[ $# -eq 1 ] || { echo "usage: $0 <report-dir>" >&2; exit 2; }
mkdir -p "$1" || exit 2
REPORT=$(cd "$1" && pwd)
ROOT=$(git rev-parse --show-toplevel) || exit 2
cd "$ROOT" || exit 2

URL=${UPSTREAM_URL:-https://github.com/endless-sky/endless-sky.git}
BRANCH=${UPSTREAM_BRANCH:-master}
REMOTE=${UPSTREAM_REMOTE:-upstream}
UPREF="$REMOTE/$BRANCH"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "error: the worktree has changes; run this on a clean checkout" >&2
	exit 2
fi
git config user.name > /dev/null || git config user.name "upstream-check"
git config user.email > /dev/null || git config user.email "upstream-check@invalid"

if ! git remote get-url "$REMOTE" > /dev/null 2>&1; then
	git remote add "$REMOTE" "$URL" || exit 2
fi
if [ "${NO_FETCH:-0}" != 1 ]; then
	git fetch --no-tags "$REMOTE" "+refs/heads/$BRANCH:refs/remotes/$UPREF" > "$REPORT/fetch.log" 2>&1 \
		|| { cat "$REPORT/fetch.log" >&2; echo "error: could not fetch $URL $BRANCH" >&2; exit 2; }
fi

HEAD_SHA=$(git rev-parse HEAD)
UP_SHA=$(git rev-parse "$UPREF") || exit 2
BASE_SHA=$(git merge-base HEAD "$UPREF") || { echo "error: no merge base with $UPREF (is the clone shallow?)" >&2; exit 2; }
BEHIND=$(git rev-list --count "HEAD..$UPREF")
AHEAD=$(git rev-list --count "$UPREF..HEAD")
WEB=${URL%.git}

status=ok
failed=()
S="$REPORT/summary.md"
{
	echo "Upstream: \`$UPREF\` at \`${UP_SHA:0:12}\`. Fork: \`$(git rev-parse --abbrev-ref HEAD)\` at \`${HEAD_SHA:0:12}\`."
	echo
	echo "Upstream commits not yet merged: **$BEHIND** ([\`${BASE_SHA:0:12}...${UP_SHA:0:12}\`]($WEB/compare/$BASE_SHA...$UP_SHA)); fork commits not upstream: $AHEAD."
	echo
} > "$S"

echo "== merging $UPREF (${UP_SHA:0:12}, $BEHIND commits) into HEAD (${HEAD_SHA:0:12})"
merged=true
if [ "$BEHIND" -eq 0 ]; then
	echo "Already up to date." > "$REPORT/merge.log"
elif git merge --no-commit --no-ff "$UPREF" > "$REPORT/merge.log" 2>&1; then
	:
else
	merged=false
	status=fail
	failed+=("merge")
fi
cat "$REPORT/merge.log"

if [ "$merged" = false ]; then
	conflicts=$(git diff --name-only --diff-filter=U)
	{
		echo "## Merge: conflicts"
		echo
		if [ -n "$conflicts" ]; then
			echo "\`git merge $UPREF\` stops with conflicts in:"
			echo
			while IFS= read -r f; do echo "- \`$f\`"; done <<< "$conflicts"
		else
			echo "\`git merge $UPREF\` failed:"
			echo
			echo '```'
			tail -n 40 "$REPORT/merge.log"
			echo '```'
		fi
		echo
		echo "The checks were not run."
	} >> "$S"
else
	{
		echo "## Merge"
		echo
		if [ "$BEHIND" -eq 0 ]; then echo "Already up to date."; else echo "Merges cleanly."; fi
		echo
		echo "## Checks"
		echo
		echo "| Check | Result |"
		echo "|---|---|"
	} >> "$S"
	details=$(mktemp)
	check() {   # check <name> <log> <command...>
		local name=$1 log=$2
		shift 2
		echo "== $name"
		if "$@" > "$REPORT/$log" 2>&1; then
			echo "| $name | passed |" >> "$S"
			tail -n 3 "$REPORT/$log"
		else
			status=fail
			failed+=("$name")
			echo "| $name | **failed** |" >> "$S"
			tail -n 40 "$REPORT/$log"
			{
				echo
				echo "### $name"
				echo
				echo "\`$*\`, the last 40 lines:"
				echo
				echo '```'
				tail -n 40 "$REPORT/$log"
				echo '```'
			} >> "$details"
		fi
	}
	check "Gameplay patches (patches/apply.sh check)" patches.log sh patches/apply.sh check
	check "Mod loader test (tests/modloader/run.sh)" modloader.log sh tests/modloader/run.sh
	check "Code style (utils/check_code_style.py)" code_style.log python3 utils/check_code_style.py
	check "CMake file lists (utils/check_cmake.sh)" cmake.log bash utils/check_cmake.sh
	cat "$details" >> "$S"
	rm -f "$details"
fi

echo "$status" > "$REPORT/status"
echo "$merged" > "$REPORT/merged"
{
	echo "head=$HEAD_SHA"
	echo "upstream=$UP_SHA"
	echo "base=$BASE_SHA"
	echo "behind=$BEHIND"
	echo "failed=${failed[*]:-}"
} > "$REPORT/info"
echo "== result: $status${failed:+ (failed: ${failed[*]})}"
exit 0
