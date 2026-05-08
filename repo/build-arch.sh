#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

pick_wlroots_package() {
    local candidates=(wlroots wlroots0.20 wlroots0.19 wlroots0.18)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    printf 'Nie znaleziono pakietu wlroots w znanych wariantach: %s\n' "${candidates[*]}" >&2
    return 1
}

wlroots_pkg="$(pick_wlroots_package)"

sudo pacman -S --needed --noconfirm \
    base-devel meson ninja pkgconf wayland wayland-protocols "$wlroots_pkg" \
    cairo pango glib2 gtk3 gtk4 libpulse libxkbcommon libinput libxml2 pixman scdoc gdk-pixbuf2

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
