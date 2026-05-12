#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
login_manager_choice="${KARTON_LOGIN_MANAGER:-}"

normalize_login_manager_choice() {
    local choice="${1,,}"

    case "$choice" in
        greetd|gtkgreet)
            printf 'greetd\n'
            return 0
            ;;
        lightdm|lightdm-gtk|lightdm-gtk-greeter)
            printf 'lightdm\n'
            return 0
            ;;
        sddm)
            printf 'sddm\n'
            return 0
            ;;
        gdm|gdm3)
            printf 'gdm\n'
            return 0
            ;;
        ly)
            printf 'ly\n'
            return 0
            ;;
        none|skip|brak|existing|system)
            printf 'none\n'
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

select_login_manager() {
    login_manager_choice="lightdm"
    echo "==> Ustawiam manager logowania na: $login_manager_choice"
    return 0
}

should_setup_greetd() {
    [[ "$login_manager_choice" == "greetd" ]]
}

should_setup_lightdm() {
    [[ "$login_manager_choice" == "lightdm" ]]
}

should_setup_sddm() {
    [[ "$login_manager_choice" == "sddm" ]]
}

should_setup_gdm() {
    [[ "$login_manager_choice" == "gdm" ]]
}

should_setup_ly() {
    [[ "$login_manager_choice" == "ly" ]]
}

setup_lightdm_karton_theme() {
    local lightdm_dir="/etc/lightdm"
    local lightdm_conf_dir="$lightdm_dir/lightdm.conf.d"
    local lightdm_conf="$lightdm_conf_dir/90-karton.conf"
    local greeter_conf="$lightdm_dir/lightdm-gtk-greeter.conf"
    local src_conf="$project_root/repo/lightdm/90-karton.conf"
    local src_greeter_conf="$project_root/repo/lightdm/lightdm-gtk-greeter.conf"

    if ! command -v sudo >/dev/null 2>&1; then
        echo "Pomijam konfiguracje LightDM: brak sudo"
        return 0
    fi

    echo "==> Konfiguruje LightDM pod sesje KartON"
    sudo install -d -m 755 "$lightdm_conf_dir"
    if [[ -f "$src_conf" ]]; then
        sudo install -m 644 "$src_conf" "$lightdm_conf"
    else
        echo "Uwaga: brak szablonu LightDM w repo: $src_conf"
    fi

    if [[ -f "$src_greeter_conf" ]]; then
        sudo install -m 644 "$src_greeter_conf" "$greeter_conf"
    else
        echo "Uwaga: brak szablonu lightdm-gtk-greeter w repo: $src_greeter_conf"
    fi

    if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files lightdm.service >/dev/null 2>&1; then
        sudo systemctl enable lightdm.service >/dev/null 2>&1 || true
    fi
}

setup_sddm_karton_theme() {
    local sddm_conf_dir="/etc/sddm.conf.d"
    local sddm_conf="$sddm_conf_dir/90-karton.conf"
    local src_conf="$project_root/repo/sddm/90-karton.conf"

    if ! command -v sudo >/dev/null 2>&1; then
        echo "Pomijam konfiguracje SDDM: brak sudo"
        return 0
    fi

    echo "==> Konfiguruje SDDM pod sesje KartON"
    sudo install -d -m 755 "$sddm_conf_dir"
    if [[ -f "$src_conf" ]]; then
        sudo install -m 644 "$src_conf" "$sddm_conf"
    else
        echo "Uwaga: brak szablonu SDDM w repo: $src_conf"
    fi

    if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files sddm.service >/dev/null 2>&1; then
        sudo systemctl enable sddm.service >/dev/null 2>&1 || true
    fi
}

setup_gdm_karton_theme() {
    local gdm_assets_dir="/usr/local/share/karton/gdm"
    local src_css="$project_root/repo/gdm/karton-shell.css"
    local src_readme="$project_root/repo/gdm/README.md"

    if ! command -v sudo >/dev/null 2>&1; then
        echo "Pomijam konfiguracje GDM: brak sudo"
        return 0
    fi

    echo "==> Konfiguruje GDM pod sesje KartON (domyslna konfiguracja)"
    sudo install -d -m 755 "$gdm_assets_dir"
    [[ -f "$src_css" ]] && sudo install -m 644 "$src_css" "$gdm_assets_dir/karton-shell.css" || true
    [[ -f "$src_readme" ]] && sudo install -m 644 "$src_readme" "$gdm_assets_dir/README.md" || true

    if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files gdm.service >/dev/null 2>&1; then
        sudo systemctl enable gdm.service >/dev/null 2>&1 || true
    elif command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files gdm3.service >/dev/null 2>&1; then
        sudo systemctl enable gdm3.service >/dev/null 2>&1 || true
    fi
}

setup_ly_karton_theme() {
    local ly_conf="/etc/ly/config.ini"
    local src_ly_conf="$project_root/repo/ly/config.ini"

    if ! command -v sudo >/dev/null 2>&1; then
        echo "Pomijam konfiguracje Ly: brak sudo"
        return 0
    fi

    echo "==> Konfiguruje Ly pod sesje KartON"
    if [[ -f "$src_ly_conf" ]]; then
        sudo install -d -m 755 /etc/ly
        sudo install -m 644 "$src_ly_conf" "$ly_conf"
    else
        echo "Uwaga: brak szablonu Ly w repo: $src_ly_conf"
    fi

    if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files ly.service >/dev/null 2>&1; then
        sudo systemctl enable ly.service >/dev/null 2>&1 || true
    fi
}

install_karton_cursor_themes() {
    local src_root="$project_root/repo/cursors"
    local dst_root="/usr/local/share/icons"
    local build_script="$src_root/build-karton-cursors.sh"
    local themes=(KartONCursorLight KartONCursorDark)
    local theme

    if [[ ! -d "$src_root" ]]; then
        return 0
    fi

    echo "==> Instaluje motywy kursorow KartON"
    sudo install -d -m 755 "$dst_root"

    if ! command -v xcursorgen >/dev/null 2>&1; then
        echo "==> Brak xcursorgen, probuje doinstalowac"
        if command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed --noconfirm xorg-xcursorgen >/dev/null 2>&1 || true
        elif command -v apt >/dev/null 2>&1; then
            sudo apt update >/dev/null 2>&1 || true
            sudo apt install -y xcursorgen >/dev/null 2>&1 || true
        elif command -v zypper >/dev/null 2>&1; then
            sudo zypper install -y xcursorgen >/dev/null 2>&1 || true
        fi
    fi

    if [[ -f "$build_script" ]]; then
        echo "==> Buduje kursory KartON"
        "$build_script" || echo "Uwaga: nie udalo sie zbudowac kursorow KartON; pozostaja fallbacki z dziedziczenia"
    fi

    for theme in "${themes[@]}"; do
        [[ -d "$src_root/$theme" ]] || continue
        sudo install -d -m 755 "$dst_root/$theme"
        sudo cp -R "$src_root/$theme"/. "$dst_root/$theme"/
    done
}

setup_selected_login_manager() {
    install_karton_cursor_themes

    case "$login_manager_choice" in
        greetd)
            setup_greetd_gtkgreet_karton_theme
            ;;
        lightdm)
            setup_lightdm_karton_theme
            ;;
        sddm)
            setup_sddm_karton_theme
            ;;
        gdm)
            setup_gdm_karton_theme
            ;;
        ly)
            setup_ly_karton_theme
            ;;
        none)
            echo "==> Pomijam konfiguracje managera logowania (wybrano: brak zmian)"
            ;;
        *)
            echo "Uwaga: nieznany manager logowania: $login_manager_choice"
            ;;
    esac
}

build_project() {
    local name="$1"
    shift || true
    local extra_setup_args=("$@")
    local src_dir="$project_root/$name"
    local build_dir="$src_dir/builddir-repo"

    if [[ ! -f "$src_dir/meson.build" ]]; then
        echo "Pomijam $name: brak meson.build"
        return 0
    fi

    echo "==> Buduję $name"
    if [[ ! -f "$build_dir/meson-private/coredata.dat" ]]; then
        meson setup "$build_dir" "$src_dir" --prefix=/usr/local "${extra_setup_args[@]}"
    else
        meson setup --reconfigure "$build_dir" "$src_dir" --prefix=/usr/local "${extra_setup_args[@]}"
    fi
    meson compile -C "$build_dir"

    echo "==> Instaluje $name"
    sudo meson install -C "$build_dir"

    if [[ "$name" == "karton-shell" && -d "$project_root/icons" ]]; then
        echo "==> Instaluje ikony shella KartON"
        sudo install -d -m 755 /usr/local/icons /usr/local/share/karton/icons
        sudo cp "$project_root"/icons/*.svg /usr/local/icons/
        sudo cp "$project_root"/icons/*.svg /usr/local/share/karton/icons/
    fi
}

build_all_karton() {
    build_project tektura -Dsystemd-session=enabled
    build_project karton-shell
    build_project karton-settings
    build_project karton-session
    build_project karton-files
    build_project karton-terminal
}

setup_greetd_gtkgreet_karton_theme() {
    local greetd_dir="/etc/greetd"
    local greetd_cfg="$greetd_dir/config.toml"
    local gtkgreet_cfg="$greetd_dir/gtkgreet.toml"
    local gtkgreet_css="$greetd_dir/gtkgreet-karton.css"
    local snippet_file="$greetd_dir/karton-default-session.toml"
    local src_cfg="$project_root/repo/greetd/gtkgreet.toml"
    local src_css="$project_root/repo/greetd/gtkgreet-karton.css"
    local managed_begin="# BEGIN KartON greetd default_session"
    local managed_end="# END KartON greetd default_session"
    local gtkgreet_bin="/usr/bin/gtkgreet"
    local has_gtkgreet=0
    local session_command
    local tmp_file
    local merged_tmp
    local stripped_tmp
    local backup_file

    if [[ ! -f "$src_cfg" || ! -f "$src_css" ]]; then
        echo "Pomijam konfiguracje greetd: brak plikow motywu w repo"
        return 0
    fi

    if ! command -v sudo >/dev/null 2>&1; then
        echo "Pomijam konfiguracje greetd: brak sudo"
        return 0
    fi

    if command -v gtkgreet >/dev/null 2>&1; then
        gtkgreet_bin="$(command -v gtkgreet)"
        has_gtkgreet=1
    elif [[ -x "$gtkgreet_bin" ]]; then
        has_gtkgreet=1
    fi

    echo "==> Instaluje motyw KartON dla gtkgreet do $greetd_dir"
    sudo install -d -m 755 "$greetd_dir"
    sudo install -m 644 "$src_cfg" "$gtkgreet_cfg"
    sudo install -m 644 "$src_css" "$gtkgreet_css"

    if [[ "$has_gtkgreet" -ne 1 ]]; then
        echo "==> Pomijam modyfikacje $greetd_cfg: binarka gtkgreet nie jest dostepna"
        return 0
    fi

    session_command="$gtkgreet_bin  --style $gtkgreet_css"
    if command -v cage >/dev/null 2>&1; then
        session_command="env WLR_NO_HARDWARE_CURSORS=1 WLR_DRM_NO_ATOMIC=1 cage -s -- $session_command"
    fi

    tmp_file="$(mktemp)"
    cat > "$tmp_file" <<EOF
$managed_begin
[default_session]
command = "$session_command"
user = "greeter"
$managed_end
EOF

    if [[ ! -f "$greetd_cfg" ]]; then
        echo "==> Tworze domyslny $greetd_cfg"
        sudo install -m 644 "$tmp_file" "$greetd_cfg"
    elif grep -q "$managed_begin" "$greetd_cfg"; then
        merged_tmp="$(mktemp)"
        {
            cat "$tmp_file"
            awk -v begin="$managed_begin" -v end="$managed_end" '
                $0 == begin { skip = 1; next }
                $0 == end { skip = 0; next }
                skip { next }
                { print }
            ' "$greetd_cfg"
        } > "$merged_tmp"
        echo "==> Aktualizuje zarzadzany blok KartON w $greetd_cfg"
        sudo install -m 644 "$merged_tmp" "$greetd_cfg"
        rm -f "$merged_tmp"
    else
        stripped_tmp="$(mktemp)"
        merged_tmp="$(mktemp)"
        backup_file="$greetd_cfg.karton.bak.$(date +%s)"

        awk '
            BEGIN { in_default = 0 }
            /^\[default_session\][[:space:]]*$/ {
                in_default = 1
                next
            }
            /^\[[^]]+\][[:space:]]*$/ {
                if (in_default) {
                    in_default = 0
                }
            }
            in_default { next }
            { print }
        ' "$greetd_cfg" > "$stripped_tmp"

        {
            cat "$tmp_file"
            if [[ -s "$stripped_tmp" ]]; then
                printf '\n'
                cat "$stripped_tmp"
            fi
        } > "$merged_tmp"

        echo "==> Wykryto wlasny $greetd_cfg; podmieniam default_session i tworzę backup"
        sudo cp "$greetd_cfg" "$backup_file"
        sudo install -m 644 "$merged_tmp" "$greetd_cfg"
        rm -f "$stripped_tmp" "$merged_tmp"
        echo "   Backup zapisany: $backup_file"
    fi

    rm -f "$tmp_file"

    if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files greetd.service >/dev/null 2>&1; then
        sudo systemctl enable greetd.service >/dev/null 2>&1 || true
    fi
}
