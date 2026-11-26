#!/usr/bin/env bash
# Sends this checkout's sources and the Windows scripts to a Windows PC over SSH, for
# tools/windows/build.ps1 there (docs/BUILDING.md "Windows"; tools/windows/README.md).
#
#   tools/windows/send.sh <user>@<pc> [root on the PC, default D:/gt4r]
#
# The sources are the checked-out commit plus any uncommitted changes to tracked files, and the
# engine submodule's checked-out commit. Extra ssh options go in $GT4R_SSH_OPTS.
set -euo pipefail
[[ $# -ge 1 ]] || { sed -n '2,9p' "$0"; exit 2; }
host=$1
root=${2:-D:/gt4r}
src=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

tree=$(git -C "$src" stash create || true)
git -C "$src" archive --format=tar.gz -o "$tmp/gt4r-src.tar.gz" "${tree:-HEAD}"
git -C "$src/engine/gt3r" archive --format=tar.gz --prefix=engine/gt3r/ -o "$tmp/gt4r-engine.tar.gz" HEAD
cp "$src"/tools/windows/*.ps1 "$src"/tools/windows/*.cmd "$tmp"/

# shellcheck disable=SC2086
ssh ${GT4R_SSH_OPTS:-} "$host" "if not exist ${root//\//\\} mkdir ${root//\//\\}"
# shellcheck disable=SC2086
scp -q ${GT4R_SSH_OPTS:-} "$tmp"/* "$host:$root/"
echo "sent to $host:$root"
