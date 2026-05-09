#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/../.." && pwd)"
pkg_dir="$script_dir/x86_64"

mkdir -p "$pkg_dir"

export PKGDEST="$pkg_dir"
export SRCDEST="$script_dir/.srcdest"
export SRCPKGDEST="$script_dir/.srcpkgdest"
export KARTONDE_REPO_URL="${KARTONDE_REPO_URL:-https://github.com/mijsys/Tektura-i-Karton.git}"

rm -rf "$SRCDEST/kartonde"

cd "$script_dir"
makepkg --syncdeps --cleanbuild --force

rm -f "$pkg_dir/kartonde.db" "$pkg_dir/kartonde.db.tar.gz" "$pkg_dir/kartonde.files" "$pkg_dir/kartonde.files.tar.gz"
repo-add "$pkg_dir/kartonde.db.tar.gz" "$pkg_dir"/*.pkg.tar.*