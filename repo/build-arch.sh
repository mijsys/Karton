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

pick_gtk_greeter_package() {
    local candidates=(greetd-gtkgreet gtkgreet greetd-gtk-greeter greettdgtk greetdgtk)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

wlroots_pkg="$(pick_wlroots_package)"
gtk_greeter_pkg="$(pick_gtk_greeter_package || true)"

packages=(
    base-devel meson ninja pkgconf wayland wayland-protocols "$wlroots_pkg"
    cairo pango glib2 gtk3 gtk4 libpulse libxkbcommon libinput libxml2 pixman scdoc gdk-pixbuf2
    greetd cage
)

if [[ -n "$gtk_greeter_pkg" ]]; then
    packages+=("$gtk_greeter_pkg")
else
    echo "Uwaga: nie znaleziono pakietu GTK greeter (greetd-gtkgreet/gtkgreet)"
fi

sudo pacman -S --needed --noconfirm "${packages[@]}"

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
setup_greetd_gtkgreet_karton_theme
