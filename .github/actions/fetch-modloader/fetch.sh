#!/bin/bash
# Argosy (not upstream): fetch.sh <platform> <dest>
#
# Downloads es-modloader-<version>-<platform>.{zip,tar.gz} and SHA256SUMS from
# the release named in $PIN_FILE (default .github/modloader-version), checks the
# archive's checksum, and puts the library in <dest>/modloaders/ and the
# loader's README and licences in <dest>/modloaders/esdado/ (the game opens only
# library files directly in modloaders/, so the subfolder is ignored).
#
# Environment: GH_TOKEN (for gh), PIN_FILE, and MODLOADER_FROM_DIR: a folder that
# already holds the release's files (for testing; then nothing is downloaded).
# Writes version= and library= to $GITHUB_OUTPUT when that is set.
set -euo pipefail

err() { echo "::error title=Mod loader::$*" >&2; exit 1; }

[ $# -eq 2 ] || err "usage: fetch.sh <platform> <dest>"
PLATFORM=$1
DEST=$2
case $PLATFORM in
	macos-universal|macos-arm64|macos-x86_64) LIB=libesdado.dylib EXT=zip ;;
	linux-x86_64|linux-aarch64) LIB=libesdado.so EXT=tar.gz ;;
	windows-x86_64) LIB=esdado.dll EXT=zip ;;
	*) err "unknown platform \"$PLATFORM\"" ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
PIN_FILE=${PIN_FILE:-$HERE/../../modloader-version}
[ -f "$PIN_FILE" ] || err "no pin file $PIN_FILE"
read -r REPO TAG < <(grep -v '^[[:space:]]*\(#\|$\)' "$PIN_FILE" | head -n 1) || true
[ -n "${REPO:-}" ] && [ -n "${TAG:-}" ] || err "$PIN_FILE should hold \"<owner>/<repo> v<version>\""
[[ $TAG == v* ]] || err "the tag in $PIN_FILE should start with v (the loader's tags are v<version>), not \"$TAG\""
VERSION=${TAG#v}
ASSET="es-modloader-$VERSION-$PLATFORM.$EXT"
echo "Mod loader: $REPO $TAG, $ASSET"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
if [ -n "${MODLOADER_FROM_DIR:-}" ]; then
	for f in "$ASSET" SHA256SUMS; do
		[ -f "$MODLOADER_FROM_DIR/$f" ] || err "$f is not in $MODLOADER_FROM_DIR"
		cp "$MODLOADER_FROM_DIR/$f" "$WORK/"
	done
else
	command -v gh > /dev/null || err "gh (the GitHub CLI) is not installed"
	gh release view "$TAG" -R "$REPO" --json tagName > /dev/null 2>&1 \
		|| err "release $TAG not found in $REPO. Publish it (es-modloader: tools/release.sh ${VERSION} --publish), change .github/modloader-version, or run with bundle_loader=false."
	gh release download "$TAG" -R "$REPO" -D "$WORK" -p "$ASSET" -p SHA256SUMS \
		|| err "could not download $ASSET and SHA256SUMS from $REPO $TAG"
	for f in "$ASSET" SHA256SUMS; do
		[ -f "$WORK/$f" ] || err "release $TAG of $REPO has no asset $f"
	done
fi

# Check the checksum: the SHA256SUMS line for this archive, and nothing else.
want=$(awk -v f="$ASSET" '$2 == f || $2 == "*" f { print $1; exit }' "$WORK/SHA256SUMS")
[ -n "$want" ] || err "SHA256SUMS of $REPO $TAG lists no $ASSET"
if command -v sha256sum > /dev/null; then
	got=$(sha256sum "$WORK/$ASSET" | awk '{ print $1 }')
else
	got=$(shasum -a 256 "$WORK/$ASSET" | awk '{ print $1 }')
fi
[ "$got" = "$want" ] || err "checksum mismatch for $ASSET: SHA256SUMS says $want, the file is $got"
echo "Checksum OK: $got"

# Extract with Python: the same on every runner (no unzip on some).
# (On Windows, "python3" may be the Microsoft Store stub, so each is tried.)
PY=
for c in python3 python py; do
	if command -v "$c" > /dev/null && "$c" -c 'import sys; sys.exit(sys.version_info < (3, 6))' > /dev/null 2>&1; then
		PY=$c
		break
	fi
done
[ -n "$PY" ] || err "no Python 3 to extract the archive"
mkdir -p "$WORK/x"
"$PY" -I - "$WORK/$ASSET" "$WORK/x" <<'PYEOF'
import sys, tarfile, zipfile, os
src, out = sys.argv[1], sys.argv[2]
if src.endswith(".zip"):
    with zipfile.ZipFile(src) as z:
        for n in z.namelist():
            if n.startswith("/") or ".." in n.split("/"):
                sys.exit("unsafe path in archive: " + n)
        z.extractall(out)
else:
    with tarfile.open(src) as t:
        for m in t.getmembers():
            if m.name.startswith("/") or ".." in m.name.split("/") or not (m.isfile() or m.isdir()):
                sys.exit("unsafe entry in archive: " + m.name)
        if hasattr(tarfile, "data_filter"):
            t.extractall(out, filter="data")
        else:
            t.extractall(out)
PYEOF
ROOT="$WORK/x/es-modloader-$VERSION-$PLATFORM"
[ -f "$ROOT/$LIB" ] || err "$ASSET has no es-modloader-$VERSION-$PLATFORM/$LIB"

mkdir -p "$DEST/modloaders/esdado"
cp "$ROOT/$LIB" "$DEST/modloaders/$LIB"
for f in README.md LICENSE LICENSE.txt LICENSE.md COPYING; do
	if [ -f "$ROOT/$f" ]; then cp "$ROOT/$f" "$DEST/modloaders/esdado/"; fi
done
if [ -d "$ROOT/licenses" ]; then cp -R "$ROOT/licenses" "$DEST/modloaders/esdado/"; fi
printf 'Dado mod loader %s from https://github.com/%s/releases/tag/%s (%s, sha256 %s)\n' \
	"$VERSION" "$REPO" "$TAG" "$ASSET" "$got" > "$DEST/modloaders/esdado/VERSION.txt"

echo "Installed $DEST/modloaders/$LIB"
ls -lR "$DEST/modloaders"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
	{
		echo "version=$VERSION"
		echo "library=$DEST/modloaders/$LIB"
	} >> "$GITHUB_OUTPUT"
fi
