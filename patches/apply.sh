#!/bin/sh
# Applies, checks or reverts the optional gameplay patches in this folder.
#   patches/apply.sh check  [patch...]   test that every patch applies cleanly
#   patches/apply.sh apply  [patch...]   apply them (after checking them all)
#   patches/apply.sh revert [patch...]   take them out again (after checking)
#   patches/apply.sh status [patch...]   say which are applied
# Without names, every *.patch here is used, in name order. Names may be given
# with or without the folder and the .patch extension. A patch that is already
# applied is skipped by apply and check, and one that is not applied is skipped
# by revert, saying so. Uses `git apply`, which works in a git checkout or in a
# plain copy of the source.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
cd "$ROOT" || exit 2

usage() {
	sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//' >&2
	exit 2
}
[ $# -ge 1 ] || usage
action=$1
shift
case "$action" in
	apply|check|revert|status) ;;
	-h|--help|help) usage ;;
	*) echo "error: unknown action \"$action\"" >&2; usage ;;
esac
if ! command -v git > /dev/null 2>&1; then
	echo "error: git is needed (it runs \`git apply\`)" >&2
	exit 2
fi

# The patches to use.
patches=""
if [ $# -eq 0 ]; then
	for p in "$HERE"/*.patch; do
		[ -e "$p" ] && patches="$patches $p"
	done
else
	for name in "$@"; do
		p=$HERE/$(basename "$name" .patch).patch
		if [ ! -f "$p" ]; then
			echo "error: no patch \"$name\" in patches/" >&2
			exit 2
		fi
		patches="$patches $p"
	done
fi
[ -n "$patches" ] || { echo "error: no patches found in patches/" >&2; exit 2; }

applies() { git apply --check "$1" > /dev/null 2>&1; }
reverts() { git apply --check -R "$1" > /dev/null 2>&1; }
name() { basename "$1"; }

# Check every patch before changing anything; keep the ones to act on.
failed=0
todo=""
for p in $patches; do
	case "$action" in
		apply|check)
			if applies "$p"; then
				echo "ok: $(name "$p") applies cleanly"
				todo="$todo $p"
			elif reverts "$p"; then
				echo "skip: $(name "$p") is already applied"
			else
				echo "error: $(name "$p") does not apply; the files it changes have moved on:" >&2
				git apply --check "$p" 2>&1 | sed 's/^/    /' >&2
				failed=1
			fi ;;
		revert)
			if reverts "$p"; then
				echo "ok: $(name "$p") can be reverted"
				todo="$todo $p"
			elif applies "$p"; then
				echo "skip: $(name "$p") is not applied"
			else
				echo "error: $(name "$p") cannot be reverted; the files it changes were edited since:" >&2
				git apply --check -R "$p" 2>&1 | sed 's/^/    /' >&2
				failed=1
			fi ;;
		status)
			if reverts "$p"; then
				echo "applied:     $(name "$p")"
			elif applies "$p"; then
				echo "not applied: $(name "$p")"
			else
				echo "conflicts:   $(name "$p") (neither applies nor reverts cleanly)"
				failed=1
			fi ;;
	esac
done
if [ "$action" = check ] || [ "$action" = status ] || [ $failed -ne 0 ]; then
	if [ $failed -ne 0 ] && { [ "$action" = apply ] || [ "$action" = revert ]; }; then
		echo "Nothing was changed." >&2
	fi
	exit $failed
fi

if [ -z "$todo" ]; then
	echo "Nothing to do."
	exit 0
fi
if [ "$action" = apply ]; then
	for p in $todo; do
		git apply "$p" || { echo "error: applying $(name "$p") failed" >&2; exit 1; }
		echo "applied: $(name "$p")"
	done
else
	# Revert in the opposite order.
	reversed=""
	for p in $todo; do
		reversed="$p $reversed"
	done
	for p in $reversed; do
		git apply -R "$p" || { echo "error: reverting $(name "$p") failed" >&2; exit 1; }
		echo "reverted: $(name "$p")"
	done
fi
