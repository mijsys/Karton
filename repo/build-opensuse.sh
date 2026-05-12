#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
select_login_manager

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
lightdm_pkg="$(pick_zypper_package lightdm || true)"
lightdm_greeter_pkg="$(pick_zypper_package lightdm-gtk-greeter lightdm-slick-greeter || true)"
sddm_pkg="$(pick_zypper_package sddm || true)"
gdm_pkg="$(pick_zypper_package gdm gdm3 || true)"
ly_pkg="$(pick_zypper_package ly || true)"
wlr_randr_pkg="$(pick_zypper_package wlr-randr wlr-rands || true)"
brightness_pkg="$(pick_zypper_package brightnessctl || true)"
ddc_pkg="$(pick_zypper_package ddcutil || true)"
night_light_pkg="$(pick_zypper_package gammastep wlsunset || true)"
terminal_vte_pkg="$(pick_zypper_package vte-devel libvte-2_91-devel || true)"
xcursorgen_pkg="$(pick_zypper_package xcursorgen || true)"
portal_pkg="$(pick_zypper_package xdg-desktop-portal || true)"
portal_backend_pkg="$(pick_zypper_package xdg-desktop-portal-gtk xdg-desktop-portal-gnome || true)"

zypper_packages=(
    gcc gcc-c++ make meson ninja pkgconf-pkg-config wayland-devel
    wayland-protocols-devel wlroots-devel cairo-devel pango-devel
    glib2-devel gtk3-devel gtk4-devel gdk-pixbuf-devel libpulse-devel
    libxkbcommon-devel libinput-devel libxml2-devel pixman-devel scdoc
)

if should_setup_greetd; then
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
else
    if should_setup_lightdm; then
        if [[ -n "$lightdm_pkg" ]]; then
            zypper_packages+=("$lightdm_pkg")
        else
            echo "Uwaga: pakiet lightdm niedostepny w repozytorium zypper"
        fi

        if [[ -n "$lightdm_greeter_pkg" ]]; then
            zypper_packages+=("$lightdm_greeter_pkg")
        else
            echo "Uwaga: nie znaleziono pakietu lightdm-gtk-greeter/lightdm-slick-greeter"
        fi
    elif should_setup_sddm; then
        if [[ -n "$sddm_pkg" ]]; then
            zypper_packages+=("$sddm_pkg")
        else
            echo "Uwaga: pakiet sddm niedostepny w repozytorium zypper"
        fi
    elif should_setup_gdm; then
        if [[ -n "$gdm_pkg" ]]; then
            zypper_packages+=("$gdm_pkg")
        else
            echo "Uwaga: pakiet gdm/gdm3 niedostepny w repozytorium zypper"
        fi
    elif should_setup_ly; then
        if [[ -n "$ly_pkg" ]]; then
            zypper_packages+=("$ly_pkg")
        else
            echo "Uwaga: pakiet ly niedostepny w repozytorium zypper"
        fi
    else
        echo "==> Pomijam instalacje managera logowania (wybrano: brak zmian)"
    fi
fi

if [[ -n "$wlr_randr_pkg" ]]; then
    zypper_packages+=("$wlr_randr_pkg")
else
    echo "Uwaga: nie znaleziono pakietu wlr-randr (monitor backend dla karton-settings)"
fi

if [[ -n "$brightness_pkg" ]]; then
    zypper_packages+=("$brightness_pkg")
else
    echo "Uwaga: nie znaleziono pakietu brightnessctl (backend jasnosci dla karton-settings)"
fi

if [[ -n "$ddc_pkg" ]]; then
    zypper_packages+=("$ddc_pkg")
else
    echo "Uwaga: nie znaleziono pakietu ddcutil (fallback jasnosci dla monitorow zewnetrznych)"
fi

if [[ -n "$night_light_pkg" ]]; then
    zypper_packages+=("$night_light_pkg")
else
    echo "Uwaga: nie znaleziono pakietu gammastep/wlsunset (backend night light dla karton-settings)"
fi

if [[ -n "$terminal_vte_pkg" ]]; then
    zypper_packages+=("$terminal_vte_pkg")
else
    echo "Uwaga: nie znaleziono pakietu VTE (wymagany przez karton-terminal)"
fi

if [[ -n "$xcursorgen_pkg" ]]; then
    zypper_packages+=("$xcursorgen_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xcursorgen (budowanie natywnych kursorow KartON)"
fi

if [[ -n "$portal_pkg" ]]; then
    zypper_packages+=("$portal_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xdg-desktop-portal (integracja portalowa)"
fi

if [[ -n "$portal_backend_pkg" ]]; then
    zypper_packages+=("$portal_backend_pkg")
else
    echo "Uwaga: nie znaleziono backendu portalu xdg-desktop-portal-gtk/gnome (interfejs Inhibit)"
fi

sudo zypper install -y "${zypper_packages[@]}"

build_all_karton
setup_selected_login_manager
