#!/usr/bin/env bash
set -euo pipefail

repo_url="${KARTON_REPO_URL:-https://github.com/mijsys/Tektura-i-Karton.git}"
workdir="${KARTON_WORKDIR:-$HOME/.cache/karton-src}"

rm -rf "$workdir"
git clone --depth 1 "$repo_url" "$workdir"
exec bash "$workdir/repo/build-arch.sh"