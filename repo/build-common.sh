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
    build_project karton-files
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

    session_command="$gtkgreet_bin --config $gtkgreet_cfg --style $gtkgreet_css"
    if command -v cage >/dev/null 2>&1; then
        session_command="cage -s -- $session_command"
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
