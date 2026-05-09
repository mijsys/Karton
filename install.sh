#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEKTURA_DIR="$SCRIPT_DIR/tektura"
SHELL_DIR="$SCRIPT_DIR/karton-shell"
SESSION_DIR="$SCRIPT_DIR/karton-session"
SETTINGS_DIR="$SCRIPT_DIR/karton-settings"
FILES_DIR="$SCRIPT_DIR/karton-files"

PREFIX="${HOME}/.local-karton"
USE_SUDO=0
SYSTEM_MODE=0
RESTART_SESSION=1
DEV_SHELL_MODE=0
SETUP_GREETD=0
CONFIG_NAME="karton"
ACTION="install"
CURRENT_SESSION_TYPE="${XDG_SESSION_TYPE:-}"
CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-${DESKTOP_SESSION:-}}"

print_usage() {
  cat <<'EOF'
Usage: ./install.sh [options] [command]

Configure, build, and optionally install the Karton compositor, shell, session,
and settings components.

Commands:
  install             Configure, build, and install all components (default)
  build               Configure and build all components without installing
  compile             Alias for build

Options:
  --prefix <path>     Install prefix (default: ~/.local-karton)
  --system            Install system-wide to /usr/local (uses sudo)
  --dev-shell         Point the running session to karton-shell/builddir-user
  --setup-greetd      Install and configure greetd + gtkgreet KartON theme
  --no-setup-greetd   Skip greetd/gtkgreet setup (default in system mode: enabled)
  --no-restart        Do not restart running karton session modules after install
  -h, --help          Show this help

Examples:
  ./install.sh
  ./install.sh build
  ./install.sh compile
  ./install.sh --prefix "$HOME/.local-karton"
  ./install.sh --dev-shell
  ./install.sh --setup-greetd
  ./install.sh --system --no-setup-greetd
  ./install.sh --system
EOF
}

log() {
  printf '[install] %s\n' "$*"
}

die() {
  printf '[install][error] %s\n' "$*" >&2
  exit 1
}

copy_optional_user_data() {
  local src="$1"
  local dest="$2"
  local dest_dir

  [[ -f "$src" ]] || return 0

  dest_dir="$(dirname "$dest")"
  if mkdir -p "$dest_dir" 2>/dev/null && cp "$src" "$dest" 2>/dev/null; then
    return 0
  fi

  log "Skipping optional user data copy (destination not writable): $dest"
  return 0
}

copy_user_desktop_file() {
  local src="$1"
  local dest="$2"
  local icon_path="$3"
  local dest_dir
  local tmp_file

  [[ -f "$src" ]] || return 0

  dest_dir="$(dirname "$dest")"
  if ! mkdir -p "$dest_dir" 2>/dev/null; then
    log "Skipping optional user data copy (destination not writable): $dest"
    return 0
  fi

  tmp_file="$(mktemp)"
  sed "s|^Icon=.*$|Icon=$icon_path|" "$src" > "$tmp_file"
  if cp "$tmp_file" "$dest" 2>/dev/null; then
    rm -f "$tmp_file"
    return 0
  fi

  rm -f "$tmp_file"
  log "Skipping optional user data copy (destination not writable): $dest"
  return 0
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

warn_screenshot_runtime_deps() {
  local missing=()
  for cmd in grim slurp wl-copy; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      missing+=("$cmd")
    fi
  done

  if [[ ${#missing[@]} -eq 0 ]]; then
    return
  fi

  log "Warning: screenshot keybinds need missing tools: ${missing[*]}"

  if command -v pacman >/dev/null 2>&1; then
    log "Install hint (Arch): sudo pacman -S --needed grim slurp wl-clipboard"
  elif command -v apt >/dev/null 2>&1; then
    log "Install hint (Debian/Ubuntu): sudo apt install grim slurp wl-clipboard"
  elif command -v dnf >/dev/null 2>&1; then
    log "Install hint (Fedora): sudo dnf install grim slurp wl-clipboard"
  elif command -v zypper >/dev/null 2>&1; then
    log "Install hint (openSUSE): sudo zypper install grim slurp wl-clipboard"
  fi
}

run_install() {
  if [[ "$USE_SUDO" -eq 1 ]]; then
    sudo meson install -C "$1"
  else
    meson install -C "$1"
  fi
}

pick_pacman_pkg() {
  local pkg
  for pkg in "$@"; do
    if pacman -Si "$pkg" >/dev/null 2>&1; then
      printf '%s\n' "$pkg"
      return 0
    fi
  done
  return 1
}

run_sudo_or_warn() {
  if sudo "$@"; then
    return 0
  fi

  log "Warning: sudo command failed: $*"
  return 1
}

pick_apt_pkg() {
  local pkg
  for pkg in "$@"; do
    if apt-cache show "$pkg" >/dev/null 2>&1; then
      printf '%s\n' "$pkg"
      return 0
    fi
  done
  return 1
}

pick_zypper_pkg() {
  local pkg
  for pkg in "$@"; do
    if zypper --non-interactive se -x "$pkg" 2>/dev/null | grep -q "<name>$pkg</name>"; then
      printf '%s\n' "$pkg"
      return 0
    fi
  done
  return 1
}

install_greetd_packages() {
  local gtk_greeter_pkg=""

  [[ "$SETUP_GREETD" -eq 1 ]] || return 0

  if command -v pacman >/dev/null 2>&1; then
    gtk_greeter_pkg="$(pick_pacman_pkg greetd-gtkgreet gtkgreet greetd-gtk-greeter greettdgtk greetdgtk || true)"
    local pacman_packages=(greetd cage)
    if [[ -n "$gtk_greeter_pkg" ]]; then
      pacman_packages+=("$gtk_greeter_pkg")
    else
      log "Warning: no GTK greeter package found (greetd-gtkgreet/gtkgreet)"
    fi

    log "Installing greetd dependencies via pacman"
    if ! run_sudo_or_warn pacman -S --needed --noconfirm "${pacman_packages[@]}"; then
      log "Warning: failed to install greetd packages via pacman; continuing"
    fi
    return 0
  fi

  if command -v apt >/dev/null 2>&1; then
    local greetd_pkg cage_pkg
    greetd_pkg="$(pick_apt_pkg greetd || true)"
    gtk_greeter_pkg="$(pick_apt_pkg gtkgreet greetd-gtkgreet greetd-gtk-greeter greettdgtk greetdgtk || true)"
    cage_pkg="$(pick_apt_pkg cage || true)"
    local apt_packages=()

    if [[ -n "$greetd_pkg" ]]; then
      apt_packages+=("$greetd_pkg")
    else
      log "Warning: greetd package not found in apt repositories"
    fi

    if [[ -n "$gtk_greeter_pkg" ]]; then
      apt_packages+=("$gtk_greeter_pkg")
    else
      log "Warning: no GTK greeter package found (gtkgreet/greetd-gtkgreet)"
    fi

    if [[ -n "$cage_pkg" ]]; then
      apt_packages+=("$cage_pkg")
    else
      log "Warning: cage package not found, gtkgreet will run without cage"
    fi

    if [[ "${#apt_packages[@]}" -gt 0 ]]; then
      log "Installing greetd dependencies via apt"
      if run_sudo_or_warn apt update; then
        if ! run_sudo_or_warn apt install -y "${apt_packages[@]}"; then
          log "Warning: failed to install greetd packages via apt; continuing"
        fi
      else
        log "Warning: apt update failed; skipping greetd package installation"
      fi
    fi
    return 0
  fi

  if command -v zypper >/dev/null 2>&1; then
    local greetd_pkg cage_pkg
    greetd_pkg="$(pick_zypper_pkg greetd || true)"
    gtk_greeter_pkg="$(pick_zypper_pkg gtkgreet greetd-gtkgreet greetd-gtk-greeter greettdgtk greetdgtk || true)"
    cage_pkg="$(pick_zypper_pkg cage || true)"
    local zypper_packages=()

    if [[ -n "$greetd_pkg" ]]; then
      zypper_packages+=("$greetd_pkg")
    else
      log "Warning: greetd package not found in zypper repositories"
    fi

    if [[ -n "$gtk_greeter_pkg" ]]; then
      zypper_packages+=("$gtk_greeter_pkg")
    else
      log "Warning: no GTK greeter package found (gtkgreet/greetd-gtkgreet)"
    fi

    if [[ -n "$cage_pkg" ]]; then
      zypper_packages+=("$cage_pkg")
    else
      log "Warning: cage package not found, gtkgreet will run without cage"
    fi

    if [[ "${#zypper_packages[@]}" -gt 0 ]]; then
      log "Installing greetd dependencies via zypper"
      if ! run_sudo_or_warn zypper install -y "${zypper_packages[@]}"; then
        log "Warning: failed to install greetd packages via zypper; continuing"
      fi
    fi
    return 0
  fi

  log "Warning: unsupported package manager; skipping greetd package installation"
  return 0
}

setup_greetd_workspace_config() {
  local greetd_dir="/etc/greetd"
  local greetd_cfg="$greetd_dir/config.toml"
  local gtkgreet_cfg="$greetd_dir/gtkgreet.toml"
  local gtkgreet_css="$greetd_dir/gtkgreet-karton.css"
  local snippet_file="$greetd_dir/karton-default-session.toml"
  local src_cfg="$SCRIPT_DIR/repo/greetd/gtkgreet.toml"
  local src_css="$SCRIPT_DIR/repo/greetd/gtkgreet-karton.css"
  local managed_begin="# BEGIN KartON greetd default_session"
  local managed_end="# END KartON greetd default_session"
  local gtkgreet_bin="/usr/bin/gtkgreet"
  local has_gtkgreet=0
  local session_command
  local tmp_file
  local merged_tmp
  local stripped_tmp
  local backup_file

  [[ "$SETUP_GREETD" -eq 1 ]] || return 0

  [[ -f "$src_cfg" ]] || die "Missing greetd config template: $src_cfg"
  [[ -f "$src_css" ]] || die "Missing greetd style template: $src_css"

  install_greetd_packages

  if command -v gtkgreet >/dev/null 2>&1; then
    gtkgreet_bin="$(command -v gtkgreet)"
    has_gtkgreet=1
  elif [[ -x "$gtkgreet_bin" ]]; then
    has_gtkgreet=1
  fi

  log "Installing KartON greetd theme to $greetd_dir"
  if ! run_sudo_or_warn install -d -m 755 "$greetd_dir"; then
    log "Warning: cannot create $greetd_dir; skipping greetd configuration"
    return 0
  fi
  if ! run_sudo_or_warn install -m 644 "$src_cfg" "$gtkgreet_cfg"; then
    log "Warning: cannot install $gtkgreet_cfg; skipping greetd configuration"
    return 0
  fi
  if ! run_sudo_or_warn install -m 644 "$src_css" "$gtkgreet_css"; then
    log "Warning: cannot install $gtkgreet_css; skipping greetd configuration"
    return 0
  fi

  if [[ "$has_gtkgreet" -ne 1 ]]; then
    log "Warning: gtkgreet binary not found; skipped default_session changes"
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
    log "Creating default greetd config: $greetd_cfg"
    if ! run_sudo_or_warn install -m 644 "$tmp_file" "$greetd_cfg"; then
      log "Warning: cannot write $greetd_cfg"
    fi
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
    log "Updating KartON managed greetd block in $greetd_cfg"
    if ! run_sudo_or_warn install -m 644 "$merged_tmp" "$greetd_cfg"; then
      log "Warning: cannot update $greetd_cfg"
    fi
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

    log "Detected custom greetd config, replacing default_session with KartON block"
    if run_sudo_or_warn cp "$greetd_cfg" "$backup_file"; then
      log "Backup created: $backup_file"
    fi
    if ! run_sudo_or_warn install -m 644 "$merged_tmp" "$greetd_cfg"; then
      log "Warning: cannot update $greetd_cfg, saving suggested snippet instead"
      cat > "$tmp_file" <<EOF
# KartON suggested default_session for greetd
[default_session]
command = "$session_command"
user = "greeter"
EOF
      if run_sudo_or_warn install -m 644 "$tmp_file" "$snippet_file"; then
        log "Saved suggested greetd session snippet: $snippet_file"
      fi
    fi

    rm -f "$stripped_tmp" "$merged_tmp"
  fi

  rm -f "$tmp_file"

  if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files greetd.service >/dev/null 2>&1; then
    log "Enabling greetd service"
    sudo systemctl enable greetd.service >/dev/null 2>&1 || true
  fi
}

prepare_installed_shell_targets() {
  if [[ "$ACTION" != "install" || "$SYSTEM_MODE" -ne 0 || "$DEV_SHELL_MODE" -eq 1 ]]; then
    return
  fi

  mkdir -p "$PREFIX/bin"

  local name target_bin backup_bin
  for name in karton-shell karton-system-status karton-dialog karton-dialogd; do
    target_bin="$PREFIX/bin/$name"
    backup_bin="$PREFIX/bin/$name.installed"

    if [[ -L "$target_bin" && ( -L "$backup_bin" || -e "$backup_bin" ) ]]; then
      log "Removing stale dev symlink before install: $target_bin"
      rm -f "$target_bin"
    fi
  done
}

cleanup_installed_shell_backups() {
  if [[ "$ACTION" != "install" || "$SYSTEM_MODE" -ne 0 || "$DEV_SHELL_MODE" -eq 1 ]]; then
    return
  fi

  local name target_bin backup_bin
  for name in karton-shell karton-system-status karton-dialog karton-dialogd; do
    target_bin="$PREFIX/bin/$name"
    backup_bin="$PREFIX/bin/$name.installed"

    if [[ ! -L "$target_bin" && ( -L "$backup_bin" || -e "$backup_bin" ) ]]; then
      log "Removing stale installed backup: $backup_bin"
      rm -f "$backup_bin"
    fi
  done
}

install_shell_icons() {
  if [[ "$ACTION" != "install" ]]; then
    return
  fi

  [[ -d "$SCRIPT_DIR/icons" ]] || return

  local dest
  for dest in "$PREFIX/icons" "$PREFIX/share/karton/icons"; do
    log "Installing Karton icons to $dest"
    if [[ "$USE_SUDO" -eq 1 ]]; then
      sudo mkdir -p "$dest"
      sudo cp "$SCRIPT_DIR"/icons/*.svg "$dest/"
    else
      mkdir -p "$dest"
      cp "$SCRIPT_DIR"/icons/*.svg "$dest/"
    fi
  done
}

build_project() {
  local src_dir="$1"
  shift
  local extra_setup_args=("$@")
  local build_dir="$src_dir/builddir"
  local non_writable=""

  if [[ "$SYSTEM_MODE" -eq 0 ]]; then
    build_dir="$src_dir/builddir-user"

    if [[ -d "$build_dir" ]]; then
      non_writable="$(find "$build_dir" -mindepth 1 ! -writable -print -quit 2>/dev/null || true)"
      if [[ -n "$non_writable" ]]; then
        local stale_dir="$build_dir.stale.$(date +%s)"
        log "Detected non-writable files in $(basename "$src_dir") build dir; rotating to $stale_dir"
        mv "$build_dir" "$stale_dir"
      fi
    fi
  fi

  log "Configuring $(basename "$src_dir")"
  if [[ -f "$build_dir/meson-private/coredata.dat" ]]; then
    meson setup "$build_dir" "$src_dir" --reconfigure --prefix "$PREFIX" "${extra_setup_args[@]}"
  else
    meson setup "$build_dir" "$src_dir" --prefix "$PREFIX" "${extra_setup_args[@]}"
  fi

  log "Building $(basename "$src_dir")"
  meson compile -C "$build_dir"

  if [[ "$ACTION" == "install" ]]; then
    if [[ "$src_dir" == "$SHELL_DIR" ]]; then
      prepare_installed_shell_targets
    fi

    log "Installing $(basename "$src_dir")"
    run_install "$build_dir"

    if [[ "$src_dir" == "$SHELL_DIR" ]]; then
      cleanup_installed_shell_backups
      install_shell_icons
    fi
  fi
}

ensure_user_config() {
  local config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/${CONFIG_NAME}"
  local autostart_file="$config_dir/autostart"
  local style_file="$config_dir/shell.css"
  local rcxml_file="$config_dir/rc.xml"
  local environment_file="$config_dir/environment"
  local desktop_src="$PREFIX/share/applications/karton-settings.desktop"
  local desktop_src_appid="$PREFIX/share/applications/io.karton.Settings.desktop"
  local files_desktop_src_appid="$PREFIX/share/applications/io.karton.Files.desktop"
  local desktop_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  local icon_src="$PREFIX/share/icons/hicolor/scalable/apps/karton-settings.svg"
  local icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Settings.svg"
  local files_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Files.svg"
  local icon_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
  local managed_ssd_block
  local xkb_layout=""
  local xkb_model=""
  local xkb_variant=""
  local xkb_options=""
  local managed_keyboard_env_block=""

  managed_ssd_block="$(cat <<'EOF'
  <!-- BEGIN KartON managed SSD rules -->
  <core>
    <decoration>server</decoration>
  </core>

  <windowRules>
    <windowRule identifier="*" serverDecoration="yes" />
    <windowRule identifier="nautilus*" serverDecoration="no" />
    <windowRule identifier="org.gnome.Nautilus*" serverDecoration="no" />
    <windowRule identifier="firefox*" serverDecoration="no" />
    <windowRule identifier="Navigator*" serverDecoration="no" />
    <windowRule identifier="code*" serverDecoration="no" />
    <windowRule identifier="Code*" serverDecoration="no" />
    <windowRule identifier="code-oss*" serverDecoration="no" />
    <windowRule identifier="Code - OSS*" serverDecoration="no" />
    <windowRule identifier="codium*" serverDecoration="no" />
    <windowRule identifier="VSCodium*" serverDecoration="no" />
    <windowRule identifier="cursor*" serverDecoration="no" />
    <windowRule identifier="Cursor*" serverDecoration="no" />
    <windowRule identifier="chromium*" serverDecoration="no" />
    <windowRule identifier="google-chrome*" serverDecoration="no" />
    <windowRule identifier="microsoft-edge*" serverDecoration="no" />
    <windowRule identifier="zen*" serverDecoration="no" />
    <windowRule identifier="org.mozilla.firefox*" serverDecoration="no" />
    <windowRule identifier="visual-studio-code*" serverDecoration="no" />
    <windowRule identifier="code-url-handler*" serverDecoration="no" />
    <windowRule identifier="discord*" serverDecoration="no" />
    <windowRule identifier="Slack*" serverDecoration="no" />
    <windowRule identifier="obsidian*" serverDecoration="no" />
  </windowRules>
  <!-- END KartON managed SSD rules -->
EOF
)"

  read_localectl_field() {
    local field="$1"
    localectl status 2>/dev/null | awk -F: -v key="$field" '$1 ~ key {
      sub(/^[[:space:]]+/, "", $2)
      print $2
      exit
    }'
  }

  write_managed_env_block() {
    local file="$1"
    local begin_marker="$2"
    local end_marker="$3"
    local block="$4"
    local tmp_file

    tmp_file="$(mktemp)"
    if [[ -f "$file" ]]; then
      awk -v begin="$begin_marker" -v end="$end_marker" '
        $0 == begin { skip = 1; next }
        $0 == end { skip = 0; next }
        skip { next }
        { print }
      ' "$file" > "$tmp_file"
    fi

    {
      printf '%s\n' "$begin_marker"
      printf '%s\n' "$block"
      printf '%s\n' "$end_marker"
      if [[ -s "$tmp_file" ]]; then
        printf '\n'
        cat "$tmp_file"
      fi
    } > "$file"

    rm -f "$tmp_file"
  }

  write_managed_ssd_block() {
    local file="$1"
    local tmp_file

    tmp_file="$(mktemp)"
    awk -v block="$managed_ssd_block" '
      BEGIN { inserted = 0; skip = 0; skip_legacy = 0 }
      /<!--[[:space:]]*BEGIN KartON managed SSD rules[[:space:]]*-->/ {
        skip = 1
        next
      }
      /<!--[[:space:]]*END KartON managed SSD rules[[:space:]]*-->/ {
        skip = 0
        next
      }
      skip {
        next
      }
      /<!--[[:space:]]*Managed by KartON installer: force server-side decorations[[:space:]]*-->/ {
        skip_legacy = 1
        next
      }
      skip_legacy {
        if ($0 ~ /<\/windowRules>/) {
          skip_legacy = 0
        }
        next
      }
      /^<labwc_config>/ || /^<openbox_config>/ {
        print
        if (!inserted) {
          print block
          inserted = 1
        }
        next
      }
      { print }
    ' "$file" > "$tmp_file"
    mv "$tmp_file" "$file"
  }

  mkdir -p "$config_dir"

  xkb_layout="$(read_localectl_field 'X11 Layout' || true)"
  xkb_model="$(read_localectl_field 'X11 Model' || true)"
  xkb_variant="$(read_localectl_field 'X11 Variant' || true)"
  xkb_options="$(read_localectl_field 'X11 Options' || true)"

  if [[ -n "$xkb_layout" || -n "$xkb_model" || -n "$xkb_variant" || -n "$xkb_options" ]]; then
    managed_keyboard_env_block=""
    if [[ -n "$xkb_layout" ]]; then
      managed_keyboard_env_block+="XKB_DEFAULT_LAYOUT=$xkb_layout"$'\n'
    fi
    if [[ -n "$xkb_model" ]]; then
      managed_keyboard_env_block+="XKB_DEFAULT_MODEL=$xkb_model"$'\n'
    fi
    if [[ -n "$xkb_variant" ]]; then
      managed_keyboard_env_block+="XKB_DEFAULT_VARIANT=$xkb_variant"$'\n'
    fi
    if [[ -n "$xkb_options" ]]; then
      managed_keyboard_env_block+="XKB_DEFAULT_OPTIONS=$xkb_options"$'\n'
    fi
    managed_keyboard_env_block="${managed_keyboard_env_block%$'\n'}"

    write_managed_env_block \
      "$environment_file" \
      "# BEGIN KartON managed keyboard env" \
      "# END KartON managed keyboard env" \
      "$managed_keyboard_env_block"
  fi

  if [[ ! -f "$autostart_file" ]]; then
    log "Creating default autostart: $autostart_file"
    cat > "$autostart_file" <<EOF
#!/bin/sh
export PATH="$PREFIX/bin:\$PATH"
karton-sessiond >/dev/null 2>&1 &
EOF
    chmod +x "$autostart_file"
  else
    log "Autostart already exists: $autostart_file"
    if ! grep -q "karton-sessiond" "$autostart_file"; then
      log "Patching autostart to include karton-sessiond bootstrap"
      cat >> "$autostart_file" <<EOF

# Added by KartON installer to ensure shell/session services start on login
export PATH="$PREFIX/bin:\$PATH"
karton-sessiond >/dev/null 2>&1 &
EOF

      if [[ ! -x "$autostart_file" ]]; then
        chmod +x "$autostart_file"
      fi
    fi
  fi

  if [[ -f "$SHELL_DIR/shell.css.example" && ! -f "$style_file" ]]; then
    log "Creating default shell style: $style_file"
    cp "$SHELL_DIR/shell.css.example" "$style_file"
  fi

  if [[ ! -f "$rcxml_file" ]]; then
    log "Creating default Tektura config: $rcxml_file"
    cat > "$rcxml_file" <<EOF
<?xml version="1.0"?>
<labwc_config>
$managed_ssd_block

  <theme>
    <name>KartONFlat</name>
    <icon>
      <theme>Adwaita</theme>
    </icon>
    <cornerRadius>14</cornerRadius>
    <keepBorder>yes</keepBorder>
    <titlebar>
      <layout>menu,desk:shade,iconify,max,close</layout>
      <showTitle>yes</showTitle>
    </titlebar>
  </theme>
</labwc_config>
EOF
  else
    log "Refreshing managed Tektura decoration rules: $rcxml_file"
    write_managed_ssd_block "$rcxml_file"
  fi

  local theme_sync_bin="$PREFIX/bin/karton-apply-theme"
  if [[ -x "$theme_sync_bin" ]]; then
    log "Syncing desktop theme/icon profile"
    "$theme_sync_bin" --sync-from-css || true
  fi

  if [[ -d "$PREFIX/share/themes/KartONFlat" && "$SYSTEM_MODE" -eq 0 ]]; then
    log "Enabling KartONFlat theme locally"
    mkdir -p "$HOME/.local/share/themes"
    rm -rf "$HOME/.local/share/themes/KartONFlat"
    cp -R "$PREFIX/share/themes/KartONFlat" "$HOME/.local/share/themes/"
  fi

  copy_user_desktop_file "$desktop_src" "$desktop_dir/karton-settings.desktop" "$icon_src_appid"
  copy_user_desktop_file "$desktop_src_appid" "$desktop_dir/io.karton.Settings.desktop" "$icon_src_appid"
  copy_user_desktop_file "$files_desktop_src_appid" "$desktop_dir/io.karton.Files.desktop" "$files_icon_src_appid"
  copy_optional_user_data "$icon_src" "$icon_dir/karton-settings.svg"
  copy_optional_user_data "$icon_src_appid" "$icon_dir/io.karton.Settings.svg"
  copy_optional_user_data "$files_icon_src_appid" "$icon_dir/io.karton.Files.svg"
}

migrate_legacy_config() {
  local root="${XDG_CONFIG_HOME:-$HOME/.config}"
  local target="$root/${CONFIG_NAME}"
  local legacy_tekstura="$root/tekstura"
  local legacy_labwc="$root/labwc"

  mkdir -p "$target"

  for legacy in "$legacy_tekstura" "$legacy_labwc"; do
    [[ -d "$legacy" ]] || continue
    log "Migrating legacy config: $legacy -> $target"

    while IFS= read -r -d '' src; do
      local rel dst
      rel="${src#${legacy}/}"
      dst="$target/$rel"

      if [[ -d "$src" ]]; then
        mkdir -p "$dst"
        continue
      fi

      mkdir -p "$(dirname "$dst")"
      if [[ ! -e "$dst" ]]; then
        cp "$src" "$dst"
      fi
    done < <(find "$legacy" -mindepth 1 -print0)
  done
}

print_post_install_notes() {
  cat <<EOF

Installed to: $PREFIX
Config dir:   ${XDG_CONFIG_HOME:-$HOME/.config}/${CONFIG_NAME}
EOF

  if [[ "$SYSTEM_MODE" -eq 0 ]]; then
    cat <<EOF

If your display manager does not see the session, ensure XDG_DATA_DIRS includes:
  $PREFIX/share
EOF
  fi

  if [[ -n "$CURRENT_SESSION_TYPE" || -n "$CURRENT_DESKTOP" ]]; then
    cat <<EOF

Current session environment detected:
  XDG_SESSION_TYPE=${CURRENT_SESSION_TYPE:-unknown}
  XDG_CURRENT_DESKTOP=${CURRENT_DESKTOP:-unknown}
EOF
  fi

  cat <<EOF

If you installed Karton while another desktop environment or compositor was already running,
log out to your display manager (or your current login/session manager), then start a fresh
Karton session so the new session files, binaries, and environment are picked up cleanly.

Note: ./install.sh restarts ${DEV_SHELL_MODE:+the development karton-shell from builddir-user and }the installed session components from the selected prefix
(for example ~/.local-karton/bin via karton-sessiond). If you are testing by manually
running a development binary such as karton-shell/builddir-user/karton-shell, restart that
development binary yourself instead of expecting install.sh to keep the dev process active.
EOF
}

switch_session_shell_to_dev() {
  if [[ "$DEV_SHELL_MODE" -ne 1 ]]; then
    return
  fi

  [[ "$ACTION" == "install" ]] || die "--dev-shell can only be used with install"
  [[ "$SYSTEM_MODE" -eq 0 ]] || die "--dev-shell is only supported in user mode"

  mkdir -p "$PREFIX/bin"
  local name dev_bin target_bin backup_bin
  local linked=0

  for name in karton-shell karton-system-status karton-dialog karton-dialogd; do
    dev_bin="$SCRIPT_DIR/karton-shell/builddir-user/$name"
    [[ -x "$dev_bin" ]] || die "Missing development binary: $dev_bin"

    target_bin="$PREFIX/bin/$name"
    backup_bin="$PREFIX/bin/$name.installed"

    if [[ -e "$target_bin" && ! -L "$target_bin" && ! -e "$backup_bin" ]]; then
      log "Saving installed $name to $backup_bin"
      mv "$target_bin" "$backup_bin"
    fi

    log "Linking session $name to dev binary: $dev_bin"
    ln -sfn "$dev_bin" "$target_bin"
    linked=1
  done

  [[ "$linked" -eq 1 ]] || die "No development shell binaries were linked"
}
restart_running_session() {
  if [[ "$RESTART_SESSION" -ne 1 ]]; then
    return
  fi

  if [[ "$EUID" -eq 0 ]]; then
    log "Skipping session restart (running as root)"
    return
  fi

  log "Restarting karton shell/session processes"
  if [[ "$DEV_SHELL_MODE" -eq 1 ]]; then
    log "Session restart uses karton-shell/builddir-user together with components from $PREFIX/bin"
  else
    log "Session restart uses installed components from $PREFIX/bin"
  fi
  pkill -f 'karton-shell --top-only' 2>/dev/null || true
  pkill -f 'karton-shell --side-only' 2>/dev/null || true
  pkill -x karton-shell 2>/dev/null || true
  pkill -f 'karton-sessiond' 2>/dev/null || true
  pkill -f 'karton-screenshot --daemon' 2>/dev/null || true
  pkill -f 'karton-settingsd' 2>/dev/null || true
  pkill -f 'karton-notifyd' 2>/dev/null || true
  pkill -f 'karton-notify-log' 2>/dev/null || true
  pkill -x karton-dialogd 2>/dev/null || true
  pkill -f "dbus-monitor --session interface='org.freedesktop.Notifications',member='Notify'" 2>/dev/null || true

  local attempts=30
  while pgrep -fa 'karton-sessiond|karton-shell --top-only|karton-shell --side-only|karton-screenshot --daemon|karton-settingsd|karton-notifyd|karton-notify-log|karton-dialogd' >/dev/null 2>&1 \
    && [[ "$attempts" -gt 0 ]]; do
    attempts=$((attempts - 1))
    sleep 0.1
  done

  if pgrep -fa 'karton-sessiond|karton-shell --top-only|karton-shell --side-only|karton-screenshot --daemon|karton-settingsd|karton-notifyd|karton-notify-log|karton-dialogd' >/dev/null 2>&1; then
    log "Forcing shutdown of leftover karton session processes"
    pkill -KILL -f 'karton-shell --top-only' 2>/dev/null || true
    pkill -KILL -f 'karton-shell --side-only' 2>/dev/null || true
    pkill -KILL -x karton-shell 2>/dev/null || true
    pkill -KILL -f 'karton-sessiond' 2>/dev/null || true
    pkill -KILL -f 'karton-screenshot --daemon' 2>/dev/null || true
    pkill -KILL -f 'karton-settingsd' 2>/dev/null || true
    pkill -KILL -f 'karton-notifyd' 2>/dev/null || true
    pkill -KILL -f 'karton-notify-log' 2>/dev/null || true
    pkill -KILL -x karton-dialogd 2>/dev/null || true
  fi

  if command -v karton >/dev/null 2>&1; then
    log "Reconfiguring compositor"
    karton --reconfigure >/dev/null 2>&1 || true
  elif [[ -x "$PREFIX/bin/karton" ]]; then
    log "Reconfiguring compositor"
    "$PREFIX/bin/karton" --reconfigure >/dev/null 2>&1 || true
  fi

  PATH="$PREFIX/bin:$PATH"
  karton-top-panel >/dev/null 2>&1 &
  karton-side-dock >/dev/null 2>&1 &
  karton-screenshot --daemon >/dev/null 2>&1 &
  karton-settingsd >/dev/null 2>&1 &
  karton-notifyd >/dev/null 2>&1 &

  sleep 1
  pgrep -fa 'karton-shell --top-only' >/dev/null 2>&1 || karton-top-panel >/dev/null 2>&1 &
  pgrep -fa 'karton-shell --side-only' >/dev/null 2>&1 || karton-side-dock >/dev/null 2>&1 &

  sleep 1
  pgrep -fa 'karton-shell --top-only|karton-shell --side-only|karton-screenshot --daemon|karton-settingsd|karton-notifyd' || true
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    install|build|compile)
      ACTION="$1"
      shift
      ;;
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      PREFIX="$2"
      shift 2
      ;;
    --system)
      SYSTEM_MODE=1
      PREFIX="/usr/local"
      USE_SUDO=1
      SETUP_GREETD=1
      shift
      ;;
    --dev-shell)
      DEV_SHELL_MODE=1
      shift
      ;;
    --setup-greetd)
      SETUP_GREETD=1
      shift
      ;;
    --no-setup-greetd)
      SETUP_GREETD=0
      shift
      ;;
    --no-restart)
      RESTART_SESSION=0
      shift
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      die "Unknown option or command: $1"
      ;;
  esac
done

if [[ "$SYSTEM_MODE" -eq 0 && "$EUID" -eq 0 ]]; then
  die "User mode install must not be run as root. Run ./install.sh (without sudo) or use --system."
fi

need_cmd meson
need_cmd ninja

[[ -d "$TEKTURA_DIR" ]] || die "Missing directory: $TEKTURA_DIR"
[[ -d "$SHELL_DIR" ]] || die "Missing directory: $SHELL_DIR"
[[ -d "$SESSION_DIR" ]] || die "Missing directory: $SESSION_DIR"
[[ -d "$SETTINGS_DIR" ]] || die "Missing directory: $SETTINGS_DIR"
[[ -d "$FILES_DIR" ]] || die "Missing directory: $FILES_DIR"

if [[ "$USE_SUDO" -eq 1 ]]; then
  need_cmd sudo
fi

if [[ "$SETUP_GREETD" -eq 1 ]]; then
  need_cmd sudo
fi

if [[ "$SYSTEM_MODE" -eq 1 ]]; then
  log "Using system mode with prefix $PREFIX"
  build_project "$TEKTURA_DIR" -Dsystemd-session=enabled
else
  log "Using user mode with prefix $PREFIX"
  build_project "$TEKTURA_DIR" -Dsystemd-session=disabled
fi

build_project "$SHELL_DIR"
build_project "$SESSION_DIR"
build_project "$SETTINGS_DIR"
build_project "$FILES_DIR"

if [[ "$ACTION" == "install" && "$SYSTEM_MODE" -eq 0 ]]; then
  migrate_legacy_config
  ensure_user_config
  warn_screenshot_runtime_deps
fi

if [[ "$ACTION" == "install" ]]; then
  setup_greetd_workspace_config
  switch_session_shell_to_dev
  restart_running_session

  log "Done."
  print_post_install_notes
else
  log "Build completed. No installation was performed."
fi
