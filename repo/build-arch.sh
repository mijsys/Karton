#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
select_login_manager

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

pick_lightdm_greeter_package() {
    local candidates=(lightdm-gtk-greeter lightdm-slick-greeter)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_wlr_randr_package() {
    local candidates=(wlr-randr wlr-rands)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_brightness_package() {
    local candidates=(brightnessctl)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_ddc_package() {
    local candidates=(ddcutil)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_night_light_package() {
    local candidates=(gammastep wlsunset)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_vte_package() {
    local candidates=(vte3 vte3-gtk4)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_portal_package() {
    local candidates=(xdg-desktop-portal)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_portal_backend_package() {
    local candidates=(xdg-desktop-portal-gtk xdg-desktop-portal-gnome)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_polkit_agent_package() {
    local candidates=(lxqt-policykit mate-polkit polkit-gnome)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_cliphist_package() {
    local candidates=(cliphist)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_image_viewer_package() {
    local candidates=(loupe imv eog ristretto)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_media_player_package() {
    local candidates=(mpv vlc totem)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_text_editor_package() {
    local candidates=(kate mousepad gedit pluma)
    local pkg

    for pkg in "${candidates[@]}"; do
        if pacman -Si "$pkg" >/dev/null 2>&1; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

pick_pdf_viewer_package() {
    local candidates=(zathura evince okular atril)
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
lightdm_greeter_pkg="$(pick_lightdm_greeter_package || true)"
wlr_randr_pkg="$(pick_wlr_randr_package || true)"
brightness_pkg="$(pick_brightness_package || true)"
ddc_pkg="$(pick_ddc_package || true)"
night_light_pkg="$(pick_night_light_package || true)"
terminal_vte_pkg="$(pick_vte_package || true)"
xcursorgen_pkg="$(pick_pacman_pkg xorg-xcursorgen xcursorgen || true)"
portal_pkg="$(pick_portal_package || true)"
portal_backend_pkg="$(pick_portal_backend_package || true)"
polkit_agent_pkg="$(pick_polkit_agent_package || true)"
cliphist_pkg="$(pick_cliphist_package || true)"
image_viewer_pkg="$(pick_image_viewer_package || true)"
media_player_pkg="$(pick_media_player_package || true)"
text_editor_pkg="$(pick_text_editor_package || true)"
pdf_viewer_pkg="$(pick_pdf_viewer_package || true)"

packages=(
    base-devel meson ninja pkgconf wayland wayland-protocols "$wlroots_pkg"
    cairo pango glib2 gtk3 gtk4 libpulse libxkbcommon libinput libxml2 pixman scdoc gdk-pixbuf2
)

if should_setup_greetd; then
    packages+=(greetd cage)

    if [[ -n "$gtk_greeter_pkg" ]]; then
        packages+=("$gtk_greeter_pkg")
    else
        echo "Uwaga: nie znaleziono pakietu GTK greeter (greetd-gtkgreet/gtkgreet)"
    fi
else
    if should_setup_lightdm; then
        packages+=(lightdm)
        if [[ -n "$lightdm_greeter_pkg" ]]; then
            packages+=("$lightdm_greeter_pkg")
        else
            echo "Uwaga: nie znaleziono pakietu lightdm-gtk-greeter/lightdm-slick-greeter"
        fi
    elif should_setup_sddm; then
        packages+=(sddm)
    elif should_setup_gdm; then
        packages+=(gdm)
    elif should_setup_ly; then
        packages+=(ly)
    else
        echo "==> Pomijam instalacje managera logowania (wybrano: brak zmian)"
    fi
fi

if [[ -n "$wlr_randr_pkg" ]]; then
    packages+=("$wlr_randr_pkg")
else
    echo "Uwaga: nie znaleziono pakietu wlr-randr (monitor backend dla karton-settings)"
fi

if [[ -n "$brightness_pkg" ]]; then
    packages+=("$brightness_pkg")
else
    echo "Uwaga: nie znaleziono pakietu brightnessctl (backend jasnosci dla karton-settings)"
fi

if [[ -n "$ddc_pkg" ]]; then
    packages+=("$ddc_pkg")
else
    echo "Uwaga: nie znaleziono pakietu ddcutil (fallback jasnosci dla monitorow zewnetrznych)"
fi

if [[ -n "$night_light_pkg" ]]; then
    packages+=("$night_light_pkg")
else
    echo "Uwaga: nie znaleziono pakietu gammastep/wlsunset (backend night light dla karton-settings)"
fi

if [[ -n "$terminal_vte_pkg" ]]; then
    packages+=("$terminal_vte_pkg")
else
    echo "Uwaga: nie znaleziono pakietu VTE (wymagany przez karton-terminal)"
fi

if [[ -n "$xcursorgen_pkg" ]]; then
    packages+=("$xcursorgen_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xcursorgen (budowanie natywnych kursorow KartON)"
fi

if [[ -n "$portal_pkg" ]]; then
    packages+=("$portal_pkg")
else
    echo "Uwaga: nie znaleziono pakietu xdg-desktop-portal (integracja portalowa)"
fi

if [[ -n "$portal_backend_pkg" ]]; then
    packages+=("$portal_backend_pkg")
else
    echo "Uwaga: nie znaleziono backendu portalu xdg-desktop-portal-gtk/gnome (interfejs Inhibit)"
fi

if [[ -n "$polkit_agent_pkg" ]]; then
    packages+=("$polkit_agent_pkg")
else
    echo "Uwaga: nie znaleziono pakietu polkit agenta (polkit-gnome/lxqt-policykit/mate-polkit)"
fi

if [[ -n "$cliphist_pkg" ]]; then
    packages+=("$cliphist_pkg")
else
    echo "Uwaga: nie znaleziono pakietu cliphist (menedzer schowka)"
fi

if [[ -n "$image_viewer_pkg" ]]; then
    packages+=("$image_viewer_pkg")
else
    echo "Uwaga: nie znaleziono pakietu image viewer (loupe/imv/eog/ristretto)"
fi

if [[ -n "$media_player_pkg" ]]; then
    packages+=("$media_player_pkg")
else
    echo "Uwaga: nie znaleziono pakietu media player (mpv/vlc/totem)"
fi

if [[ -n "$text_editor_pkg" ]]; then
    packages+=("$text_editor_pkg")
else
    echo "Uwaga: nie znaleziono pakietu text editor (kate/mousepad/gedit/pluma)"
fi

if [[ -n "$pdf_viewer_pkg" ]]; then
    packages+=("$pdf_viewer_pkg")
else
    echo "Uwaga: nie znaleziono pakietu PDF viewer (zathura/evince/okular/atril)"
fi

sudo pacman -S --needed --noconfirm "${packages[@]}"

build_all_karton
setup_selected_login_manager
run_phase_a_smoke_tests
run_phase_b_smoke_tests
run_phase_c_smoke_tests
