#!/bin/sh
# Mod API 11 in the real game: builds api11_loader.c, makes a scratch config
# folder with the integration tests' plugin, this folder's test data and the
# loader, and runs the game on the "API 11" test (headless, with xvfb-run).
# Then it checks the save that test wrote: the runtime definitions come before
# the ships, and a fresh game (no loader) loads it with no errors.
#   ES   the game (default: build/endless-sky)
#   OUT  the scratch folder (default: a temporary one)
# Linux only (xvfb-run, .so).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
ES=${ES:-$ROOT/build/endless-sky}
OUT=${OUT:-$(mktemp -d)}
CC=${CC:-cc}
CONFIG="$OUT/config"

rm -rf "$CONFIG"
mkdir -p "$CONFIG/modloaders"
cp -r "$ROOT/tests/integration/config/." "$CONFIG/"
mkdir -p "$CONFIG/plugins/api11-test/data"
cp "$HERE/api11_tests.txt" "$CONFIG/plugins/api11-test/data/"
"$CC" -std=c11 -Wall -Wextra -pedantic -Werror -fPIC -shared -fvisibility=hidden \
	-o "$CONFIG/modloaders/api11.so" "$HERE/api11_loader.c" -lm

status=0
timeout 600 xvfb-run --auto-servernum --server-args="+extension GLX +render -noreset" \
	"$ES" --resources "$ROOT" --test "API 11" --config "$CONFIG" > "$OUT/test.log" 2>&1 || status=1
grep -E "API11 FAIL|Test (passed|failed)|[Ff]ailed" "$OUT/test.log" || true
if [ $status -ne 0 ]; then
	echo "FAIL: the \"API 11\" test failed (log: $OUT/test.log)"
	exit 1
fi
echo "ok  : the \"API 11\" test passed"

# The save it wrote: definitions before the ships.
SAVE=$(sed -n 1p "$CONFIG/recent.txt")
defs=$(grep -n '^"runtime definitions"' "$SAVE" | head -1 | cut -d: -f1)
ship=$(grep -n '^ship ' "$SAVE" | head -1 | cut -d: -f1)
if [ -z "$defs" ] || [ -z "$ship" ] || [ "$defs" -ge "$ship" ]; then
	echo "FAIL: the save's runtime definitions do not come before its ships ($SAVE)"
	exit 1
fi
echo "ok  : the save's runtime definitions (line $defs) come before its ships (line $ship)"

# A fresh game, without the loader, loads that save with no errors.
rm -f "$CONFIG/modloaders/api11.so"
if ! "$ES" --resources "$ROOT" --config "$CONFIG" --parse-save > "$OUT/parse.log" 2>&1 \
		|| ! grep -q "Parse completed with no error(s)." "$OUT/parse.log"; then
	cat "$OUT/parse.log"
	echo "FAIL: the saved pilot does not parse cleanly without the mod"
	exit 1
fi
echo "ok  : the saved pilot parses with no errors without the mod"
