#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
select_login_manager

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
lightdm_pkg="$(pick_apt_package lightdm || true)"
lightdm_greeter_pkg="$(pick_apt_package lightdm-gtk-greeter slick-greeter || true)"
sddm_pkg="$(pick_apt_package sddm || true)"
gdm_pkg="$(pick_apt_package gdm3 gdm || true)"
ly_pkg="$(pick_apt_package ly || true)"
wlr_randr_pkg="$(pick_apt_package wlr-randr wlr-rands || true)"
brightness_pkg="$(pick_apt_package brightnessctl || true)"
ddc_pkg="$(pick_apt_package ddcutil || true)"
night_light_pkg="$(pick_apt_package gammastep wlsunset || true)"
terminal_vte_pkg="$(pick_apt_package libvte-2.91-dev || true)"
xcursorgen_pkg="$(pick_apt_package xcursorgen || true)"
portal_pkg="$(pick_apt_package xdg-desktop-portal || true)"
portal_backend_pkg="$(pick_apt_package xdg-desktop-portal-gtk xdg-desktop-portal-gnome || true)"

apt_packages=(
    build-essential meson ninja-build pkg-config wayland-protocols
    libwayland-dev libwlroots-dev libcairo2-dev libpango1.0-dev
    libglib2.0-dev libgtk-3-dev libgtk-4-dev libgdk-pixbuf-2.0-dev
    libpulse-dev libxkbcommon-dev libinput-dev libxml2-dev libpixman-1-dev scdoc
)

if should_setup_greetd; then
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
else
    if should_setup_lightdm; then
        if [[ -n "$lightdm_pkg" ]]; then
            apt_packages+=("$lightdm_pkg")
        else
            echo "Uwaga: pakiet lightdm niedostepny w repozytorium apt"
        fi

        if [[ -n "$lightdm_greeter_pkg" ]]; then
            apt_packages+=("$lightdm_greeter_pkg")
        else
            echo "Uwaga: nie znaleziono pakietu lightdm-gtk-greeter/slick-greeter"
        fi
    elif should_setup_sddm; then
        if [[ -n "$sddm_pkg" ]]; then
            apt_packages+=("$sddm_pkg")
        else
            echo "Uwaga: pakiet sddm niedostepny w repozytorium apt"
        fi
    elif should_setup_gdm; then
        if [[ -n "$gdm_pkg" ]]; then
            apt_packages+=("$gdm_pkg")
        else
            echo "Uwaga: pakiet gdm/gdm3 niedostepny w repozytorium apt"
        fi
    elif should_setup_ly; then
        if [[ -n "$ly_pkg" ]]; then
            apt_packages+=("$ly_pkg")
        else
            echo "Uwaga: pakiet ly niedostepny w repozytorium apt"
        fi
    else
        echo "==> Pomijam instalacje managera logowania (wybrano: brak zmian)"
    fi
fi

if [[ -n "$wlr_randr_pkg" ]]; then
    apt_packages+=("$wlr_randr_pkg")
else
    echo "Uwaga: nie znaleziono pakietu wlr-randr (monitor backend dla karton-settings)"
fi

if [[ -n "$brightness_pkg" ]]; then
    apt_packages+=("$brightness_pkg")
else
    echo "Uwaga: nie znaleziono pakietu brightnessctl (backend jasnosci dla karton-settings)"
fi

if [[ -n "$ddc_pkg" ]]; then
    apt_packages+=("$ddc_pkg")
else
    echo "Uwaga: nie znaleziono pakietu ddcutil (fallback jasnosci dla monitorow zewnetrznych)"
fi

if [[ -n "$night_light_pkg" ]]; then
    apt_packages+=("$night_light_pkg")
else
    echo "Uwaga: nie znaleziono pakietu gammastep/wlsunset (backend night light dla karton-settings)"
fi

if [[ -n "$terminal_vte_pkg" ]]; then
    apt_packages+=("$terminal_vte_pkg")
else
    echo "Uwaga: nie znaleziono pakietu libvte-2.91-dev (wymagany przez karton-terminal)"
fi

if [[ -n "$xcursorgen_pkg" ]]; then
    apt_packages+=("$xcursorgen_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xcursorgen (budowanie natywnych kursorow KartON)"
fi

if [[ -n "$portal_pkg" ]]; then
    apt_packages+=("$portal_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xdg-desktop-portal (integracja portalowa)"
fi

if [[ -n "$portal_backend_pkg" ]]; then
    apt_packages+=("$portal_backend_pkg")
else
    echo "Uwaga: nie znaleziono backendu portalu xdg-desktop-portal-gtk/gnome (interfejs Inhibit)"
fi

sudo apt update
sudo apt install -y "${apt_packages[@]}"

build_all_karton
setup_selected_login_manager
