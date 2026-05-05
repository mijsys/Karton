#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

build_project() {
    local name="$1"
    local src_dir="$project_root/$name"
    local build_dir="$src_dir/builddir-repo"

    if [[ ! -f "$src_dir/meson.build" ]]; then
        echo "Pomijam $name: brak meson.build"
        return 0
    fi

    echo "==> Buduję $name"
    if [[ ! -f "$build_dir/meson-private/coredata.dat" ]]; then
        meson setup "$build_dir" "$src_dir" --prefix=/usr/local
    else
        meson setup --reconfigure "$build_dir" "$src_dir" --prefix=/usr/local
    fi
    meson compile -C "$build_dir"
}

build_all_karton() {
    build_project tektura
    build_project karton-shell
    build_project karton-settings
    build_project karton-session
}
