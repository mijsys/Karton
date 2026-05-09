#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

pick_zypper_package() {
    local pkg
    for pkg in "$@"; do
        if zypper --non-interactive se -x "$pkg" 2>/dev/null | grep -q "<name>$pkg</name>"; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

greetd_pkg="$(pick_zypper_package greetd || true)"
gtk_greeter_pkg="$(pick_zypper_package gtkgreet greetd-gtkgreet greetd-gtk-greeter greettdgtk greetdgtk || true)"
cage_pkg="$(pick_zypper_package cage || true)"

zypper_packages=(
    gcc gcc-c++ make meson ninja pkgconf-pkg-config wayland-devel
    wayland-protocols-devel wlroots-devel cairo-devel pango-devel
    glib2-devel gtk3-devel gtk4-devel gdk-pixbuf-devel libpulse-devel
    libxkbcommon-devel libinput-devel libxml2-devel pixman-devel scdoc
)

if [[ -n "$greetd_pkg" ]]; then
    zypper_packages+=("$greetd_pkg")
else
    echo "Uwaga: pakiet greetd niedostepny w repozytorium zypper"
fi

if [[ -n "$gtk_greeter_pkg" ]]; then
    zypper_packages+=("$gtk_greeter_pkg")
else
    echo "Uwaga: nie znaleziono pakietu GTK greeter (gtkgreet/greetd-gtkgreet)"
fi

if [[ -n "$cage_pkg" ]]; then
    zypper_packages+=("$cage_pkg")
else
    echo "Uwaga: pakiet cage niedostepny, gtkgreet bedzie uruchamiany bez cage"
fi

sudo zypper install -y "${zypper_packages[@]}"

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
setup_greetd_gtkgreet_karton_theme
