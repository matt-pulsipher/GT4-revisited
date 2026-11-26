#!/usr/bin/env bash
# Builds a release package of GT4 Revisited from a clean build folder, as CI's package job does
# (.github/workflows/build.yml), and writes it with its checksum:
#
#   tools/release/package.sh [--out <folder>] [--build <folder>] [--keep-build]
#
#   macOS (arm64): <out>/gt4r-macos-arm64.zip     (gt4r.app, ad-hoc signed, and gt4tool)
#   Linux:         <out>/gt4r-linux-<arch>.tar.gz (bin/, share/)
#   both:          <out>/SHA256SUMS
#
# Each archive holds one folder, gt4r-v<version>-<platform>, with the app, gt4tool, README.md,
# LICENSE, THIRD-PARTY-NOTICES.md and the font licenses. No game data, no tests. SDL is built
# from source (GT3R_FETCH_SDL), so the package needs no SDL on the player's computer.
# Defaults: --out dist/release, --build build-release (removed first unless --keep-build).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/dist/release"
BUILD="$ROOT/build-release"
KEEP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --build) BUILD="$2"; shift 2 ;;
    --keep-build) KEEP=1; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

VERSION="$(sed -n 's/^project(GT4Revisited VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "no version in CMakeLists.txt" >&2; exit 1; }
[ -f "$ROOT/engine/gt3r/CMakeLists.txt" ] || { echo "engine/gt3r is empty: git submodule update --init" >&2; exit 1; }

case "$(uname -s)" in
  Darwin) PLATFORM="macos-$(uname -m)"
          EXTRA=(-DGT4R_MAC_BUNDLE=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "-DCMAKE_OSX_ARCHITECTURES=$(uname -m)") ;;
  Linux)  PLATFORM="linux-$(uname -m)"; EXTRA=() ;;
  *) echo "this script packages macOS and Linux; Windows packages come from CI" >&2; exit 1 ;;
esac
PKG="gt4r-v$VERSION-$PLATFORM"
JOBS="${JOBS:-4}"

[ "$KEEP" = 1 ] || rm -rf "$BUILD"
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S "$ROOT" -B "$BUILD" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release -DGT3R_FETCH_SDL=ON -DGT4R_BUILD_TESTS=OFF ${EXTRA[@]+"${EXTRA[@]}"}
nice -n 15 cmake --build "$BUILD" --config Release --parallel "$JOBS" --target gt4r gt4tool

STAGE="$BUILD/stage"
rm -rf "$STAGE"
cmake --install "$BUILD" --config Release --prefix "$STAGE/$PKG"

mkdir -p "$OUT"
case "$PLATFORM" in
  macos-*)
    APP="$STAGE/$PKG/gt4r.app"
    codesign --verify --deep --strict "$APP"
    plutil -lint "$APP/Contents/Info.plist" >/dev/null
    ARCHIVE="gt4r-$PLATFORM.zip"
    rm -f "$OUT/$ARCHIVE"
    ditto -c -k --keepParent "$STAGE/$PKG" "$OUT/$ARCHIVE" ;;
  linux-*)
    ARCHIVE="gt4r-$PLATFORM.tar.gz"
    tar -C "$STAGE" -czf "$OUT/$ARCHIVE" "$PKG" ;;
esac

# Nothing but what a package should hold (no tests, no developer tools, no disc images).
if (cd "$STAGE" && find "$PKG" | grep -Ei 'gt4r_tests|gt4r_diff|\.(iso|chd|bin|vol)$'); then
  echo "the package holds files it must not" >&2; exit 1
fi

cd "$OUT"
if command -v sha256sum >/dev/null; then sha256sum "$ARCHIVE" > SHA256SUMS; else shasum -a 256 "$ARCHIVE" > SHA256SUMS; fi
cat SHA256SUMS
echo "$OUT/$ARCHIVE ($PKG)"
