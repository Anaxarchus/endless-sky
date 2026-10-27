#!/bin/sh
# Builds and runs the mod loader test: the test loader variants (test_loader.c)
# and the host test (test_host.cpp with source/modding/ModLoaders.cpp), with a C
# and a C++ compiler and SDL2's headers; no libraries beyond the C library (and
# libdl on Linux). Runs on Linux (.so) and macOS (.dylib: same commands, no -ldl;
# on macOS point SDL_INCLUDE at e.g. "$(brew --prefix)/include").
#   SDL_INCLUDE  the folder that holds SDL2/SDL.h (default: /usr/include)
#   OUT          where to build (default: a temporary folder)
#   MUTATE       0 to skip the mutation checks
# Also checks that ModHarness.cpp fills every field of the host table, and that
# the identity tests fail against deliberately broken ModLoaders code.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SDL_INCLUDE=${SDL_INCLUDE:-/usr/include}
OUT=${OUT:-$(mktemp -d)}
CC=${CC:-cc}
CXX=${CXX:-c++}
MUTATE=${MUTATE:-1}
case "$(uname -s)" in
	Darwin) EXT=.dylib; LDL= ;;
	*) EXT=.so; LDL=-ldl ;;
esac
RES="$OUT/resources/modloaders"
CFG="$OUT/config/modloaders"
mkdir -p "$RES" "$CFG"

loader() {
	out=$1; shift
	"$CC" -std=c11 -Wall -Wextra -pedantic -Werror -fPIC -shared -fvisibility=hidden "$@" \
		-o "$out" "$HERE/test_loader.c"
}
loader "$RES/a_alpha$EXT" '-DTL_NAME="alpha (resources)"' '-DTL_MOD="alpha_mod"'
loader "$CFG/a_alpha$EXT" '-DTL_NAME="alpha (config)"' '-DTL_MOD="alpha_mod"' -DTL_MISBEHAVE
loader "$RES/b_newer$EXT" '-DTL_NAME="newer"' '-DTL_MOD="newer_mod"' '-DTL_VERSION=(ES_API_VERSION+1)' \
	'-DTL_SIZE=(sizeof(EsLoader)+16)'
loader "$RES/b_old_version$EXT" '-DTL_NAME="old version"' '-DTL_VERSION=(ES_API_MIN_VERSION-1)'
loader "$RES/c_small$EXT" '-DTL_NAME="small"' '-DTL_SIZE=8'
loader "$RES/d_declines$EXT" '-DTL_NAME="declines"' '-DTL_RESULT=3'
loader "$RES/e_no_init$EXT" -DTL_NO_INIT
loader "$CFG/z_beta$EXT" '-DTL_NAME="beta"' '-DTL_MOD="beta_mod"'
# A non-ASCII file name: opened through its UTF-8 path.
loader "$CFG/y_ünï$EXT" '-DTL_NAME="unicode"' '-DTL_MOD="unicode_mod"'
echo "not a library" > "$RES/f_junk$EXT"
echo "not a loader" > "$RES/notes.txt"

host() {
	out=$1; dir=$2; werror=${3:--Werror}
	"$CXX" -std=c++20 -Wall -Wextra -pedantic-errors -Wold-style-cast $werror \
		-I"$dir" -I"$SDL_INCLUDE" -o "$out" "$HERE/test_host.cpp" "$dir/ModLoaders.cpp" $LDL
}
host "$OUT/test-host" "$ROOT/source/modding"

# Every EsHost function must be set in ModHarness.cpp's Host().
missing=0
for field in $(sed -n '/^typedef struct EsHost {/,/^} EsHost;/p' "$ROOT/source/modding/es_api.h" \
		| sed -n 's/.*(ES_CALL \*\([a-z_0-9]*\))(.*/\1/p'); do
	if ! grep -q "host\.$field = es_$field;" "$ROOT/source/ModHarness.cpp"; then
		echo "FAIL: ModHarness.cpp does not set EsHost::$field"
		missing=$((missing + 1))
	fi
done
fields=$(sed -n '/^typedef struct EsHost {/,/^} EsHost;/p' "$ROOT/source/modding/es_api.h" | grep -c '(ES_CALL \*')
echo "host table: $fields functions, $missing not set by the game"

status=0
"$OUT/test-host" "$OUT/resources" "$OUT/config" || status=1
[ "$missing" -eq 0 ] || status=1

# Mutation checks: the identity tests must fail against broken bookkeeping.
#  1. RunningMod answers whatever mod was last begun (no ownership or loader check).
#  2. Calls into loaders neither clear nor restore the bracket, so one a call
#     left open stays open.
if [ "$MUTATE" != 0 ]; then
	for m in 1 2; do
		dir="$OUT/mutant$m"
		mkdir -p "$dir"
		cp "$ROOT/source/modding/ModLoaders.cpp" "$ROOT/source/modding/ModLoaders.h" \
			"$ROOT/source/modding/es_api.h" "$dir/"
		sed -i.bak -e 's|#include "\.\./|#include "'"$ROOT"'/source/|' "$dir/ModLoaders.cpp"
		if [ $m = 1 ]; then
			sed -i.bak -e 's/if(!HookOpen() || !bracket.owned || current == NONE)/if(!bracket.open)/' \
				-e 's/if(it == owners.end() || it->second != current)/if(false \&\& it == owners.end())/' \
				"$dir/ModLoaders.cpp"
		else
			sed -i.bak -e '/^inline ModLoaders::CallScope::CallScope/,/^}/s/loaders.bracket = Bracket();//' \
				-e '/^inline ModLoaders::CallScope::~CallScope/,/^}/s/loaders.bracket = std::move(outerBracket);//' \
				-e 's/outerBracket(std::move(loaders.bracket))/outerBracket(loaders.bracket)/' "$dir/ModLoaders.h"
		fi
		if cmp -s "$dir/ModLoaders.cpp" "$ROOT/source/modding/ModLoaders.cpp" \
				&& cmp -s "$dir/ModLoaders.h" "$ROOT/source/modding/ModLoaders.h"; then
			echo "FAIL: mutant $m changed nothing"; status=1; continue
		fi
		host "$OUT/mutant$m/test-host" "$dir" -Wno-unused
		if "$OUT/mutant$m/test-host" "$OUT/resources" "$OUT/config" > "$dir/out.txt" 2>&1; then
			echo "FAIL: mutant $m passed the tests"; status=1
		else
			echo "mutant $m caught by: $(grep '^FAIL' "$dir/out.txt" | sed 's/^FAIL: //' | paste -sd ';' -)"
		fi
	done
fi
exit $status
