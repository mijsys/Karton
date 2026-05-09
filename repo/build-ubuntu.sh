#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

pick_apt_package() {
    local pkg
    for pkg in "$@"; do
        if apt-cache show "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

greetd_pkg="$(pick_apt_package greetd || true)"
gtk_greeter_pkg="$(pick_apt_package gtkgreet greetd-gtkgreet greetd-gtk-greeter greettdgtk greetdgtk || true)"
cage_pkg="$(pick_apt_package cage || true)"

apt_packages=(
    build-essential meson ninja-build pkg-config wayland-protocols
    libwayland-dev libwlroots-dev libcairo2-dev libpango1.0-dev
    libglib2.0-dev libgtk-3-dev libgtk-4-dev libgdk-pixbuf-2.0-dev
    libpulse-dev libxkbcommon-dev libinput-dev libxml2-dev libpixman-1-dev scdoc
)

if [[ -n "$greetd_pkg" ]]; then
    apt_packages+=("$greetd_pkg")
else
    echo "Uwaga: pakiet greetd niedostepny w repozytorium apt"
fi

if [[ -n "$gtk_greeter_pkg" ]]; then
    apt_packages+=("$gtk_greeter_pkg")
else
    echo "Uwaga: nie znaleziono pakietu GTK greeter (gtkgreet/greetd-gtkgreet)"
fi

if [[ -n "$cage_pkg" ]]; then
    apt_packages+=("$cage_pkg")
else
    echo "Uwaga: pakiet cage niedostepny, gtkgreet bedzie uruchamiany bez cage"
fi

sudo apt update
sudo apt install -y "${apt_packages[@]}"

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
setup_greetd_gtkgreet_karton_theme
