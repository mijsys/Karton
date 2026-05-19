#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEKTURA_DIR="$SCRIPT_DIR/tektura"
SHELL_DIR="$SCRIPT_DIR/karton-shell"
SESSION_DIR="$SCRIPT_DIR/karton-session"
DAEMON_DIR="$SCRIPT_DIR/karton-idle"
LOCK_DIR="$SCRIPT_DIR/karton-lock"
SETTINGS_DIR="$SCRIPT_DIR/karton-settings"
FILES_DIR="$SCRIPT_DIR/karton-files"
TERMINAL_DIR="$SCRIPT_DIR/karton-terminal"
INSTALLER_DIR="$SCRIPT_DIR/karton-installer"

PREFIX="${HOME}/.local-karton"
USE_SUDO=0
SYSTEM_MODE=0
RESTART_SESSION=1
DEV_SHELL_MODE=0
LIVE_ISO_MODE=0
LOGIN_MANAGER_CHOICE="${KARTON_LOGIN_MANAGER:-}"
LOGIN_MANAGER_EXPLICIT=0
CONFIG_NAME="karton"
ACTION="install"
CURRENT_SESSION_TYPE="${XDG_SESSION_TYPE:-}"
CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-${DESKTOP_SESSION:-}}"

print_usage() {
  cat <<'EOF'
Usage: ./install.sh [options] [command]

Configure, build, and optionally install the Karton compositor, shell, session,
idle, lock, settings, files, terminal, and installer components.

Commands:
  install             Configure, build, and install all components (default)
  build               Configure and build all components without installing
  compile             Alias for build

Options:
  --prefix <path>     Install prefix (default: ~/.local-karton)
  --system            Install system-wide to /usr/local (uses sudo)
  --dev-shell         Point the running session to karton-shell/builddir-user
  --setup-login-manager <name>
                      Configure login manager: greetd|lightdm|sddm|gdm|ly|none
  --no-setup-login-manager
                      Skip login manager setup (equivalent to: --setup-login-manager none)
  --setup-greetd      Install and configure greetd + gtkgreet KartON theme
  --live-iso          Force live ISO mode (enable install entry in live session)
  --copytoram         Alias for --live-iso
  --no-setup-greetd   Skip greetd/gtkgreet setup (default in system mode: enabled)
  --no-restart        Do not restart running karton session modules after install
  -h, --help          Show this help

Examples:
  ./install.sh
  ./install.sh build
  ./install.sh compile
  ./install.sh --prefix "$HOME/.local-karton"
  ./install.sh --dev-shell
  ./install.sh --setup-login-manager lightdm
  ./install.sh --setup-login-manager none
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
  local exec_path="${4:-}"
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
  if [[ -n "$exec_path" ]]; then
    sed -i "s|^Exec=.*$|Exec=$exec_path|" "$tmp_file"
  fi
  if cp "$tmp_file" "$dest" 2>/dev/null; then
    rm -f "$tmp_file"
    return 0
  fi

  rm -f "$tmp_file"
  log "Skipping optional user data copy (destination not writable): $dest"
  return 0
}

set_mime_default_if_possible() {
  local desktop_id="$1"
  shift
  local mime

  if ! command -v xdg-mime >/dev/null 2>&1; then
    return 0
  fi

  for mime in "$@"; do
    xdg-mime default "$desktop_id" "$mime" >/dev/null 2>&1 || true
  done
}

is_live_iso_environment() {
  [[ "$LIVE_ISO_MODE" -eq 1 ]] \
    || [[ -e /run/archiso/bootmnt ]] \
    || [[ -e /run/archiso/cowspace ]] \
    || [[ -e /.archiso ]] \
    || grep -qi 'archiso' /etc/hostname 2>/dev/null
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
  fi
}

warn_monitor_runtime_deps() {
  if command -v wlr-randr >/dev/null 2>&1; then
    return
  fi

  log "Warning: monitor controls in karton-settings may run in limited mode; missing recommended tool: wlr-randr"

  if command -v swaymsg >/dev/null 2>&1; then
    log "Info: swaymsg is available as a fallback backend, but wlr-randr is recommended for wlroots compositors"
  fi

  if command -v pacman >/dev/null 2>&1; then
    log "Install hint (Arch): sudo pacman -S --needed wlr-randr"
  elif command -v apt >/dev/null 2>&1; then
    log "Install hint (Debian/Ubuntu): sudo apt install wlr-randr"
  elif command -v dnf >/dev/null 2>&1; then
    log "Install hint (Fedora): sudo dnf install wlr-randr"
  fi
}

warn_display_tweak_runtime_deps() {
  local has_brightness_backend=0
  local has_night_light_backend=0

  if command -v karton-system-status >/dev/null 2>&1 \
      || command -v brightnessctl >/dev/null 2>&1 \
      || command -v ddcutil >/dev/null 2>&1; then
    has_brightness_backend=1
  fi

  if command -v gammastep >/dev/null 2>&1 || command -v wlsunset >/dev/null 2>&1; then
    has_night_light_backend=1
  fi

  if [[ "$has_brightness_backend" -eq 1 && "$has_night_light_backend" -eq 1 ]]; then
    return
  fi

  if [[ "$has_brightness_backend" -eq 0 ]]; then
    log "Warning: brightness controls may not work (missing backend: karton-system-status, brightnessctl or ddcutil)"
  fi

  if [[ "$has_night_light_backend" -eq 0 ]]; then
    log "Warning: night light may not work (missing backend: gammastep or wlsunset)"
  fi

  if command -v pacman >/dev/null 2>&1; then
    log "Install hint (Arch): sudo pacman -S --needed brightnessctl ddcutil gammastep"
  elif command -v apt >/dev/null 2>&1; then
    log "Install hint (Debian/Ubuntu): sudo apt install brightnessctl ddcutil gammastep"
  elif command -v dnf >/dev/null 2>&1; then
    log "Install hint (Fedora): sudo dnf install brightnessctl ddcutil gammastep"
  fi
}

warn_portal_runtime_deps() {
  if command -v xdg-desktop-portal >/dev/null 2>&1 \
      && (command -v xdg-desktop-portal-gtk >/dev/null 2>&1 \
          || command -v xdg-desktop-portal-gnome >/dev/null 2>&1); then
    return
  fi

  log "Warning: portal Inhibit interface may be unavailable (missing xdg-desktop-portal and/or GTK/GNOME backend)"

  if command -v pacman >/dev/null 2>&1; then
    log "Install hint (Arch): sudo pacman -S --needed xdg-desktop-portal xdg-desktop-portal-gtk"
  elif command -v apt >/dev/null 2>&1; then
    log "Install hint (Debian/Ubuntu): sudo apt install xdg-desktop-portal xdg-desktop-portal-gtk"
  elif command -v dnf >/dev/null 2>&1; then
    log "Install hint (Fedora): sudo dnf install xdg-desktop-portal xdg-desktop-portal-gtk"
  fi
}

warn_stage1_core_runtime_deps() {
  local missing=()

  for cmd in swaylock swayidle wlopm; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      missing+=("$cmd")
    fi
  done

  if ! command -v cliphist >/dev/null 2>&1; then
    missing+=("cliphist")
  fi

  if ! command -v /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v /usr/libexec/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v /usr/lib/polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v /usr/libexec/polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v /usr/lib/lxqt-policykit-agent/lxqt-policykit-agent >/dev/null 2>&1 \
      && ! command -v /usr/libexec/lxqt-policykit-agent >/dev/null 2>&1 \
      && ! command -v /usr/lib/mate-polkit/polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v /usr/libexec/polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
      && ! command -v mate-polkit >/dev/null 2>&1; then
    missing+=("polkit-agent")
  fi

  if [[ ${#missing[@]} -eq 0 ]]; then
    return
  fi

  log "Warning: Stage 1 core features may be limited; missing tools: ${missing[*]}"

  if command -v pacman >/dev/null 2>&1; then
    log "Install hint (Arch): sudo pacman -S --needed swaylock swayidle wlopm kanshi cliphist polkit-gnome"
  elif command -v apt >/dev/null 2>&1; then
    log "Install hint (Debian/Ubuntu): sudo apt install swaylock swayidle cliphist policykit-1-gnome"
  elif command -v dnf >/dev/null 2>&1; then
    log "Install hint (Fedora): sudo dnf install swaylock swayidle wlroots-utils cliphist polkit-gnome"
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

prompt_login_manager_choice() {
  log "Defaulting login manager setup to: lightdm"
  LOGIN_MANAGER_CHOICE="lightdm"
  return 0
}

resolve_login_manager_choice() {
  local default_option

  if [[ "$ACTION" != "install" ]]; then
    if [[ -n "$LOGIN_MANAGER_CHOICE" ]]; then
      if ! LOGIN_MANAGER_CHOICE="$(normalize_login_manager_choice "$LOGIN_MANAGER_CHOICE")"; then
        die "Invalid login manager value: $LOGIN_MANAGER_CHOICE (expected: greetd|lightdm|sddm|gdm|ly|none)"
      fi
    else
      LOGIN_MANAGER_CHOICE="none"
    fi
    return 0
  fi

  if [[ -n "$LOGIN_MANAGER_CHOICE" ]]; then
    if ! LOGIN_MANAGER_CHOICE="$(normalize_login_manager_choice "$LOGIN_MANAGER_CHOICE")"; then
      die "Invalid login manager value: $LOGIN_MANAGER_CHOICE (expected: greetd|lightdm|sddm|gdm|ly|none)"
    fi
    return 0
  fi

  if [[ "$SYSTEM_MODE" -eq 1 ]]; then
    default_option=1
  else
    default_option=6
  fi

  if [[ ! -t 0 || ! -t 1 ]]; then
    case "$default_option" in
      1) LOGIN_MANAGER_CHOICE="lightdm" ;;
      *) LOGIN_MANAGER_CHOICE="none" ;;
    esac
    log "No TTY detected, defaulting login manager setup to: $LOGIN_MANAGER_CHOICE"
    return 0
  fi

  prompt_login_manager_choice "$default_option"
}

should_setup_greetd() {
  [[ "$LOGIN_MANAGER_CHOICE" == "greetd" ]]
}

should_setup_lightdm() {
  [[ "$LOGIN_MANAGER_CHOICE" == "lightdm" ]]
}

should_setup_sddm() {
  [[ "$LOGIN_MANAGER_CHOICE" == "sddm" ]]
}

should_setup_gdm() {
  [[ "$LOGIN_MANAGER_CHOICE" == "gdm" ]]
}

should_setup_ly() {
  [[ "$LOGIN_MANAGER_CHOICE" == "ly" ]]
}

install_greetd_packages() {
  local gtk_greeter_pkg=""

  should_setup_greetd || return 0

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

  log "Warning: unsupported package manager; skipping greetd package installation"
  return 0
}

install_lightdm_packages() {
  local greeter_pkg=""

  should_setup_lightdm || return 0

  if command -v pacman >/dev/null 2>&1; then
    greeter_pkg="$(pick_pacman_pkg lightdm-gtk-greeter lightdm-slick-greeter || true)"
    local pacman_packages=(lightdm)
    if [[ -n "$greeter_pkg" ]]; then
      pacman_packages+=("$greeter_pkg")
    else
      log "Warning: no LightDM greeter package found (lightdm-gtk-greeter/lightdm-slick-greeter)"
    fi

    log "Installing LightDM dependencies via pacman"
    run_sudo_or_warn pacman -S --needed --noconfirm "${pacman_packages[@]}" || true
    return 0
  fi

  if command -v apt >/dev/null 2>&1; then
    local lightdm_pkg
    lightdm_pkg="$(pick_apt_pkg lightdm || true)"
    greeter_pkg="$(pick_apt_pkg lightdm-gtk-greeter slick-greeter || true)"
    local apt_packages=()

    [[ -n "$lightdm_pkg" ]] && apt_packages+=("$lightdm_pkg") || log "Warning: lightdm package not found in apt repositories"
    [[ -n "$greeter_pkg" ]] && apt_packages+=("$greeter_pkg") || log "Warning: no LightDM greeter package found (lightdm-gtk-greeter/slick-greeter)"

    if [[ "${#apt_packages[@]}" -gt 0 ]]; then
      log "Installing LightDM dependencies via apt"
      run_sudo_or_warn apt update || true
      run_sudo_or_warn apt install -y "${apt_packages[@]}" || true
    fi
    return 0
  fi

  log "Warning: unsupported package manager; skipping LightDM package installation"
  return 0
}

install_sddm_packages() {
  should_setup_sddm || return 0

  if command -v pacman >/dev/null 2>&1; then
    log "Installing SDDM via pacman"
    run_sudo_or_warn pacman -S --needed --noconfirm sddm || true
    return 0
  fi

  if command -v apt >/dev/null 2>&1; then
    local sddm_pkg
    sddm_pkg="$(pick_apt_pkg sddm || true)"
    if [[ -n "$sddm_pkg" ]]; then
      log "Installing SDDM via apt"
      run_sudo_or_warn apt update || true
      run_sudo_or_warn apt install -y "$sddm_pkg" || true
    else
      log "Warning: sddm package not found in apt repositories"
    fi
    return 0
  fi

  log "Warning: unsupported package manager; skipping SDDM package installation"
  return 0
}

install_gdm_packages() {
  should_setup_gdm || return 0

  if command -v pacman >/dev/null 2>&1; then
    log "Installing GDM via pacman"
    run_sudo_or_warn pacman -S --needed --noconfirm gdm || true
    return 0
  fi

  if command -v apt >/dev/null 2>&1; then
    local gdm_pkg
    gdm_pkg="$(pick_apt_pkg gdm3 gdm || true)"
    if [[ -n "$gdm_pkg" ]]; then
      log "Installing GDM via apt"
      run_sudo_or_warn apt update || true
      run_sudo_or_warn apt install -y "$gdm_pkg" || true
    else
      log "Warning: gdm/gdm3 package not found in apt repositories"
    fi
    return 0
  fi

  log "Warning: unsupported package manager; skipping GDM package installation"
  return 0
}

install_ly_packages() {
  should_setup_ly || return 0

  if command -v pacman >/dev/null 2>&1; then
    log "Installing Ly via pacman"
    run_sudo_or_warn pacman -S --needed --noconfirm ly || true
    return 0
  fi

  if command -v apt >/dev/null 2>&1; then
    local ly_pkg
    ly_pkg="$(pick_apt_pkg ly || true)"
    if [[ -n "$ly_pkg" ]]; then
      log "Installing Ly via apt"
      run_sudo_or_warn apt update || true
      run_sudo_or_warn apt install -y "$ly_pkg" || true
    else
      log "Warning: ly package not found in apt repositories"
    fi
    return 0
  fi

  log "Warning: unsupported package manager; skipping Ly package installation"
  return 0
}

install_login_manager_packages() {
  install_greetd_packages
  install_lightdm_packages
  install_sddm_packages
  install_gdm_packages
  install_ly_packages
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

  should_setup_greetd || return 0

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

setup_lightdm_workspace_config() {
  local lightdm_dir="/etc/lightdm"
  local lightdm_conf_dir="$lightdm_dir/lightdm.conf.d"
  local lightdm_conf="$lightdm_conf_dir/90-karton.conf"
  local greeter_conf="$lightdm_dir/lightdm-gtk-greeter.conf"
  local src_conf="$SCRIPT_DIR/repo/lightdm/90-karton.conf"
  local src_greeter_conf="$SCRIPT_DIR/repo/lightdm/lightdm-gtk-greeter.conf"

  should_setup_lightdm || return 0

  log "Configuring LightDM for KartON session"
  run_sudo_or_warn install -d -m 755 "$lightdm_conf_dir" || return 0

  if [[ -d "$SCRIPT_DIR/repo/lightdm/Karton-LightDM" ]]; then
    log "Installing GTK theme: Karton-LightDM"
    run_sudo_or_warn install -d -m 755 /usr/share/themes/Karton-LightDM/gtk-3.0
    run_sudo_or_warn install -m 644 "$SCRIPT_DIR/repo/lightdm/Karton-LightDM/index.theme" /usr/share/themes/Karton-LightDM/
    run_sudo_or_warn install -m 644 "$SCRIPT_DIR/repo/lightdm/Karton-LightDM/gtk-3.0/gtk.css" /usr/share/themes/Karton-LightDM/gtk-3.0/
  fi

  if [[ -f "$src_conf" ]]; then
    if ! run_sudo_or_warn install -m 644 "$src_conf" "$lightdm_conf"; then
      log "Warning: cannot install $lightdm_conf"
      return 0
    fi
  else
    log "Warning: missing repo lightdm config template: $src_conf"
  fi

  if [[ -f "$src_greeter_conf" ]]; then
    run_sudo_or_warn install -m 644 "$src_greeter_conf" "$greeter_conf" || true
  else
    log "Warning: missing repo lightdm greeter template: $src_greeter_conf"
  fi

  if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files lightdm.service >/dev/null 2>&1; then
    log "Enabling lightdm service"
    sudo systemctl enable lightdm.service >/dev/null 2>&1 || true
  fi
}

setup_sddm_workspace_config() {
  local sddm_conf_dir="/etc/sddm.conf.d"
  local sddm_conf="$sddm_conf_dir/90-karton.conf"
  local src_conf="$SCRIPT_DIR/repo/sddm/90-karton.conf"

  should_setup_sddm || return 0

  log "Configuring SDDM for KartON session"
  run_sudo_or_warn install -d -m 755 "$sddm_conf_dir" || return 0
  if [[ -f "$src_conf" ]]; then
    run_sudo_or_warn install -m 644 "$src_conf" "$sddm_conf" || true
  else
    log "Warning: missing repo sddm config template: $src_conf"
  fi

  if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files sddm.service >/dev/null 2>&1; then
    log "Enabling sddm service"
    sudo systemctl enable sddm.service >/dev/null 2>&1 || true
  fi
}

setup_gdm_workspace_config() {
  local gdm_assets_dir="/usr/local/share/karton/gdm"
  local src_css="$SCRIPT_DIR/repo/gdm/karton-shell.css"
  local src_readme="$SCRIPT_DIR/repo/gdm/README.md"

  should_setup_gdm || return 0

  log "Configuring GDM for KartON session"
  if run_sudo_or_warn install -d -m 755 "$gdm_assets_dir"; then
    [[ -f "$src_css" ]] && run_sudo_or_warn install -m 644 "$src_css" "$gdm_assets_dir/karton-shell.css" || true
    [[ -f "$src_readme" ]] && run_sudo_or_warn install -m 644 "$src_readme" "$gdm_assets_dir/README.md" || true
  fi

  if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files gdm.service >/dev/null 2>&1; then
    log "Enabling gdm service"
    sudo systemctl enable gdm.service >/dev/null 2>&1 || true
  elif command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files gdm3.service >/dev/null 2>&1; then
    log "Enabling gdm3 service"
    sudo systemctl enable gdm3.service >/dev/null 2>&1 || true
  else
    log "Warning: gdm service unit not found"
  fi
}

setup_ly_workspace_config() {
  local ly_conf="/etc/ly/config.ini"
  local src_ly_conf="$SCRIPT_DIR/repo/ly/config.ini"

  should_setup_ly || return 0

  log "Configuring Ly for KartON session"
  if [[ -f "$src_ly_conf" ]]; then
    run_sudo_or_warn install -d -m 755 /etc/ly || true
    run_sudo_or_warn install -m 644 "$src_ly_conf" "$ly_conf" || true
  else
    log "Warning: missing repo Ly config template: $src_ly_conf"
  fi

  if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files ly.service >/dev/null 2>&1; then
    log "Enabling ly service"
    sudo systemctl enable ly.service >/dev/null 2>&1 || true
  else
    log "Warning: ly service unit not found"
  fi
}

setup_selected_login_manager() {
  case "$LOGIN_MANAGER_CHOICE" in
    greetd)
      setup_greetd_workspace_config
      ;;
    lightdm)
      setup_lightdm_workspace_config
      ;;
    sddm)
      setup_sddm_workspace_config
      ;;
    gdm)
      setup_gdm_workspace_config
      ;;
    ly)
      setup_ly_workspace_config
      ;;
    none)
      log "Skipping login manager setup"
      ;;
    *)
      log "Warning: unknown login manager choice: $LOGIN_MANAGER_CHOICE"
      ;;
  esac
}

prepare_installed_shell_targets() {
  if [[ "$ACTION" != "install" || "$SYSTEM_MODE" -ne 0 || "$DEV_SHELL_MODE" -eq 1 ]]; then
    return
  fi

  mkdir -p "$PREFIX/bin"

  local name target_bin backup_bin
  for name in karton-shell karton-system-status; do
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
  for name in karton-shell karton-system-status; do
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

install_cursor_build_tool() {
  if command -v xcursorgen >/dev/null 2>&1; then
    return 0
  fi

  log "xcursorgen not found, attempting to install cursor build dependency"

  if command -v pacman >/dev/null 2>&1; then
    # Arch Linux: pakiet to xorg-xcursorgen
    run_sudo_or_warn pacman -S --needed --noconfirm xorg-xcursorgen || true
  elif command -v apt >/dev/null 2>&1; then
    # Debian/Ubuntu: pakiet to x11-apps (zawiera xcursorgen)
    run_sudo_or_warn apt update || true
    run_sudo_or_warn apt install -y x11-apps || true
  elif command -v dnf >/dev/null 2>&1; then
    # Fedora/RHEL: pakiet to xorg-x11-apps
    run_sudo_or_warn dnf install -y xorg-x11-apps || true
  fi

  # Ponowne sprawdzenie po próbie instalacji
  if ! command -v xcursorgen >/dev/null 2>&1; then
    log "Warning: xcursorgen still unavailable, KartON cursor theme will use inherited fallbacks"
  fi
}

install_cursor_themes() {
  if [[ "$ACTION" != "install" ]]; then
    return
  fi

  local src_root="$SCRIPT_DIR/repo/cursors"
  [[ -d "$src_root" ]] || return

  install_cursor_build_tool

  local build_script="$src_root/build-karton-cursors.sh"
  if [[ -f "$build_script" ]]; then
    log "Building KartON cursor binaries"
    if ! "$build_script"; then
      log "Warning: failed to build KartON cursor binaries, continuing with inherited cursor fallback"
    fi
  fi

  local themes=(KartONCursorLight KartONCursorDark)
  local theme

  log "Installing KartON cursor themes"
  for theme in "${themes[@]}"; do
    local src_dir="$src_root/$theme"
    [[ -d "$src_dir" ]] || continue

    if [[ "$USE_SUDO" -eq 1 ]]; then
      local dst_sys="/usr/local/share/icons/$theme"
      sudo mkdir -p "$dst_sys"
      sudo cp -R "$src_dir"/. "$dst_sys"/
    else
      local dst_user="${XDG_DATA_HOME:-$HOME/.local/share}/icons/$theme"
      mkdir -p "$dst_user"
      cp -R "$src_dir"/. "$dst_user"/

      local dst_prefix="$PREFIX/share/icons/$theme"
      mkdir -p "$dst_prefix"
      cp -R "$src_dir"/. "$dst_prefix"/
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
  local wallpaper_override_file="$config_dir/wallpaper-path"
  local wallpaper_source_dir="$SCRIPT_DIR/Wallpaper"
  local default_wallpaper=""
  local autostart_file="$config_dir/autostart"
  local preferred_sessiond="$PREFIX/bin/karton-sessiond"
  local style_file="$config_dir/shell.css"
  local rcxml_file="$config_dir/rc.xml"
  local environment_file="$config_dir/environment"
  local desktop_src="$PREFIX/share/applications/karton-settings.desktop"
  local desktop_src_appid="$PREFIX/share/applications/io.karton.Settings.desktop"
  local files_desktop_src_appid="$PREFIX/share/applications/io.karton.Files.desktop"
  local terminal_desktop_src_appid="$PREFIX/share/applications/io.karton.Terminal.desktop"
  local images_desktop_src_appid="$PREFIX/share/applications/io.karton.Images.desktop"
  local media_desktop_src_appid="$PREFIX/share/applications/io.karton.Media.desktop"
  local text_desktop_src_appid="$PREFIX/share/applications/io.karton.Text.desktop"
  local pdf_desktop_src_appid="$PREFIX/share/applications/io.karton.PDF.desktop"
  local welcome_desktop_src_appid="$PREFIX/share/applications/io.karton.Welcome.desktop"
  local install_desktop_src_appid="$PREFIX/share/applications/io.karton.Install.desktop"
  local shop_desktop_src_appid="$PREFIX/share/applications/io.karton.Shop.desktop"
  local desktop_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  local icon_src="$PREFIX/share/icons/hicolor/scalable/apps/karton-settings.svg"
  local icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Settings.svg"
  local files_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Files.svg"
  local terminal_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Terminal.svg"
  local images_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Images.svg"
  local media_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Media.svg"
  local text_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Text.svg"
  local pdf_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.PDF.svg"
  local welcome_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Welcome.svg"
  local install_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Install.svg"
  local shop_icon_src_appid="$PREFIX/share/icons/hicolor/scalable/apps/io.karton.Shop.svg"
  local settings_exec="$PREFIX/bin/karton-settings"
  local files_exec="$PREFIX/bin/karton-files"
  local terminal_exec="$PREFIX/bin/karton-terminal"
  local welcome_exec="$PREFIX/bin/karton-welcome"
  local install_exec="$PREFIX/bin/karton-install"
  local shop_exec="$PREFIX/bin/karton-shop"
  local icon_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
  local managed_ssd_block
  local xkb_layout=""
  local xkb_model=""
  local xkb_variant=""
  local xkb_options=""
  local managed_keyboard_env_block=""
  local managed_autostart_block=""

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

  write_managed_autostart_block() {
    local file="$1"
    local block="$2"
    local begin_marker="# BEGIN KartON managed session bootstrap"
    local end_marker="# END KartON managed session bootstrap"
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
      if [[ -s "$tmp_file" ]]; then
        cat "$tmp_file"
        printf '\n'
      elif [[ ! -f "$file" ]]; then
        printf '#!/bin/sh\n\n'
      fi
      printf '%s\n' "$begin_marker"
      printf '%s\n' "$block"
      printf '%s\n' "$end_marker"
    } > "$file"

    rm -f "$tmp_file"
    chmod +x "$file"
  }

  mkdir -p "$config_dir"

  if [[ -d "$wallpaper_source_dir" ]]; then
    for candidate in \
      "$wallpaper_source_dir/karton.png" \
      "$wallpaper_source_dir/karton2.png"; do
      if [[ -f "$candidate" ]]; then
        default_wallpaper="$candidate"
        break
      fi
    done

    if [[ -z "$default_wallpaper" ]]; then
      while IFS= read -r -d '' candidate; do
        default_wallpaper="$candidate"
        break
      done < <(find "$wallpaper_source_dir" -maxdepth 1 -type f \
        \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.webp' \) \
        -print0 | sort -z)
    fi
  fi

  if [[ -n "$default_wallpaper" ]]; then
    if printf '%s\n' "$default_wallpaper" > "$wallpaper_override_file" 2>/dev/null; then
      log "Setting default wallpaper from Wallpaper: $default_wallpaper"
    else
      log "Warning: failed to write default wallpaper path to $wallpaper_override_file"
    fi
  fi

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

  if [[ ! -x "$preferred_sessiond" ]]; then
    preferred_sessiond="$(command -v karton-sessiond 2>/dev/null || true)"
  fi
  if [[ -n "$preferred_sessiond" ]]; then
    managed_autostart_block="export PATH=\"$PREFIX/bin:\$PATH\""
    managed_autostart_block+=$'\n'
    managed_autostart_block+="\"$preferred_sessiond\" >/dev/null 2>&1 &"
    managed_autostart_block+=$'\n'
    managed_autostart_block+="\"$welcome_exec\" --auto >/dev/null 2>&1 &"

    if [[ -f "$autostart_file" ]]; then
      log "Autostart already exists: $autostart_file"
      # Remove legacy installer lines so only one managed bootstrap remains.
      sed -i "\|^export PATH=\"$PREFIX/bin:\\$PATH\"$|d" "$autostart_file"
      sed -i "\|^karton-sessiond >/dev/null 2>&1 &$|d" "$autostart_file"
      sed -i "\|^\"$welcome_exec\" --auto >/dev/null 2>&1 &$|d" "$autostart_file"
    else
      log "Creating default autostart: $autostart_file"
    fi

    write_managed_autostart_block "$autostart_file" "$managed_autostart_block"
  else
    log "Warning: karton-sessiond not found; skipping managed autostart bootstrap"
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

  copy_user_desktop_file "$desktop_src" "$desktop_dir/karton-settings.desktop" "$icon_src_appid" "$settings_exec"
  copy_user_desktop_file "$desktop_src_appid" "$desktop_dir/io.karton.Settings.desktop" "$icon_src_appid" "$settings_exec"
  copy_user_desktop_file "$files_desktop_src_appid" "$desktop_dir/io.karton.Files.desktop" "$files_icon_src_appid" "$files_exec"
  copy_user_desktop_file "$terminal_desktop_src_appid" "$desktop_dir/io.karton.Terminal.desktop" "$terminal_icon_src_appid" "$terminal_exec"
  copy_user_desktop_file "$images_desktop_src_appid" "$desktop_dir/io.karton.Images.desktop" "io.karton.Images" "$PREFIX/bin/karton-images"
  copy_user_desktop_file "$media_desktop_src_appid" "$desktop_dir/io.karton.Media.desktop" "io.karton.Media" "$PREFIX/bin/karton-media"
  copy_user_desktop_file "$text_desktop_src_appid" "$desktop_dir/io.karton.Text.desktop" "io.karton.Text" "$PREFIX/bin/karton-text"
  copy_user_desktop_file "$pdf_desktop_src_appid" "$desktop_dir/io.karton.PDF.desktop" "io.karton.PDF" "$PREFIX/bin/karton-pdf"
  copy_user_desktop_file "$welcome_desktop_src_appid" "$desktop_dir/io.karton.Welcome.desktop" "io.karton.Welcome" "$welcome_exec"
  copy_user_desktop_file "$shop_desktop_src_appid" "$desktop_dir/io.karton.Shop.desktop" "io.karton.Shop" "$shop_exec"
  if is_live_iso_environment; then
    copy_user_desktop_file "$install_desktop_src_appid" "$desktop_dir/io.karton.Install.desktop" "io.karton.Install" "$install_exec"
    if [[ -f "$desktop_dir/io.karton.Install.desktop" ]]; then
      if grep -q '^NoDisplay=' "$desktop_dir/io.karton.Install.desktop"; then
        sed -i 's/^NoDisplay=.*/NoDisplay=false/' "$desktop_dir/io.karton.Install.desktop"
      else
        printf '\nNoDisplay=false\n' >> "$desktop_dir/io.karton.Install.desktop"
      fi
    fi
  else
    rm -f "$desktop_dir/io.karton.Install.desktop" || true
  fi
  copy_optional_user_data "$icon_src" "$icon_dir/karton-settings.svg"
  copy_optional_user_data "$icon_src_appid" "$icon_dir/io.karton.Settings.svg"
  copy_optional_user_data "$files_icon_src_appid" "$icon_dir/io.karton.Files.svg"
  copy_optional_user_data "$terminal_icon_src_appid" "$icon_dir/io.karton.Terminal.svg"
  copy_optional_user_data "$images_icon_src_appid" "$icon_dir/io.karton.Images.svg"
  copy_optional_user_data "$media_icon_src_appid" "$icon_dir/io.karton.Media.svg"
  copy_optional_user_data "$text_icon_src_appid" "$icon_dir/io.karton.Text.svg"
  copy_optional_user_data "$pdf_icon_src_appid" "$icon_dir/io.karton.PDF.svg"
  copy_optional_user_data "$welcome_icon_src_appid" "$icon_dir/io.karton.Welcome.svg"
  copy_optional_user_data "$install_icon_src_appid" "$icon_dir/io.karton.Install.svg"
  copy_optional_user_data "$shop_icon_src_appid" "$icon_dir/io.karton.Shop.svg"

  # Phase C: ensure defaults are owned by KartON apps instead of browser fallbacks.
  set_mime_default_if_possible "io.karton.Images.desktop" \
    "image/png" "image/jpeg" "image/webp" "image/svg+xml" "image/gif" "image/bmp"
  set_mime_default_if_possible "io.karton.Media.desktop" \
    "video/mp4" "video/x-matroska" "video/webm" "video/x-msvideo" "audio/mpeg" "audio/flac" "audio/x-wav" "audio/ogg"
  set_mime_default_if_possible "io.karton.Text.desktop" \
    "text/plain" "text/markdown" "text/x-log" "application/json" "application/x-yaml" "text/x-ini"
  set_mime_default_if_possible "io.karton.PDF.desktop" "application/pdf"
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

Selected login manager setup: ${LOGIN_MANAGER_CHOICE:-none}
EOF

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

  for name in karton-shell karton-system-status; do
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
  local session_process_pattern='karton-sessiond|karton-shell --top-only|karton-shell --side-only|karton-shell --desktop-only|karton-screenshot --daemon|karton-idle|karton-settingsd|karton-notifyd|karton-notify-log'
  pkill -f 'karton-shell --top-only' 2>/dev/null || true
  pkill -f 'karton-shell --side-only' 2>/dev/null || true
  pkill -f 'karton-shell --desktop-only' 2>/dev/null || true
  pkill -x karton-shell 2>/dev/null || true
  pkill -f 'karton-sessiond' 2>/dev/null || true
  pkill -f 'karton-screenshot --daemon' 2>/dev/null || true
  pkill -f 'karton-idle' 2>/dev/null || true
  pkill -f 'karton-settingsd' 2>/dev/null || true
  pkill -f 'karton-notifyd' 2>/dev/null || true
  pkill -f 'karton-notify-log' 2>/dev/null || true
  pkill -f "dbus-monitor --session interface='org.freedesktop.Notifications',member='Notify'" 2>/dev/null || true

  local attempts=30
  while pgrep -fa "$session_process_pattern" >/dev/null 2>&1 \
    && [[ "$attempts" -gt 0 ]]; do
    attempts=$((attempts - 1))
    sleep 0.1
  done

  if pgrep -fa "$session_process_pattern" >/dev/null 2>&1; then
    log "Forcing shutdown of leftover karton session processes"
    pkill -KILL -f 'karton-shell --top-only' 2>/dev/null || true
    pkill -KILL -f 'karton-shell --side-only' 2>/dev/null || true
    pkill -KILL -f 'karton-shell --desktop-only' 2>/dev/null || true
    pkill -KILL -x karton-shell 2>/dev/null || true
    pkill -KILL -f 'karton-sessiond' 2>/dev/null || true
    pkill -KILL -f 'karton-screenshot --daemon' 2>/dev/null || true
    pkill -KILL -f 'karton-idle' 2>/dev/null || true
    pkill -KILL -f 'karton-settingsd' 2>/dev/null || true
    pkill -KILL -f 'karton-notifyd' 2>/dev/null || true
    pkill -KILL -f 'karton-notify-log' 2>/dev/null || true
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
  karton-desktop >/dev/null 2>&1 &
  karton-screenshot --daemon >/dev/null 2>&1 &
  karton-settingsd >/dev/null 2>&1 &
  karton-notifyd >/dev/null 2>&1 &

  sleep 1
  pgrep -fa 'karton-shell --top-only' >/dev/null 2>&1 || karton-top-panel >/dev/null 2>&1 &
  pgrep -fa 'karton-shell --side-only' >/dev/null 2>&1 || karton-side-dock >/dev/null 2>&1 &
  pgrep -fa 'karton-shell --desktop-only' >/dev/null 2>&1 || karton-desktop >/dev/null 2>&1 &

  sleep 1
  pgrep -fa 'karton-shell --top-only|karton-shell --side-only|karton-shell --desktop-only|karton-screenshot --daemon|karton-idle|karton-settingsd|karton-notifyd' || true
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
      if [[ "$LOGIN_MANAGER_EXPLICIT" -eq 0 && -z "$LOGIN_MANAGER_CHOICE" ]]; then
        LOGIN_MANAGER_CHOICE="lightdm"
      fi
      shift
      ;;
    --dev-shell)
      DEV_SHELL_MODE=1
      shift
      ;;
    --setup-login-manager)
      [[ $# -ge 2 ]] || die "--setup-login-manager requires a value"
      if ! LOGIN_MANAGER_CHOICE="$(normalize_login_manager_choice "$2")"; then
        die "Invalid login manager value: $2 (expected: greetd|lightdm|sddm|gdm|ly|none)"
      fi
      LOGIN_MANAGER_EXPLICIT=1
      shift 2
      ;;
    --no-setup-login-manager)
      LOGIN_MANAGER_CHOICE="none"
      LOGIN_MANAGER_EXPLICIT=1
      shift
      ;;
    --setup-greetd)
      LOGIN_MANAGER_CHOICE="lightdm"
      LOGIN_MANAGER_EXPLICIT=1
      shift
      ;;
    --no-setup-greetd)
      LOGIN_MANAGER_CHOICE="none"
      LOGIN_MANAGER_EXPLICIT=1
      shift
      ;;
    --live-iso|--copytoram)
      LIVE_ISO_MODE=1
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

resolve_login_manager_choice

if [[ "$SYSTEM_MODE" -eq 0 && "$EUID" -eq 0 ]]; then
  die "User mode install must not be run as root. Run ./install.sh (without sudo) or use --system."
fi

need_cmd meson
need_cmd ninja

[[ -d "$TEKTURA_DIR" ]] || die "Missing directory: $TEKTURA_DIR"
[[ -d "$SHELL_DIR" ]] || die "Missing directory: $SHELL_DIR"
[[ -d "$SESSION_DIR" ]] || die "Missing directory: $SESSION_DIR"
  [[ -d "$DAEMON_DIR" ]] || die "Missing directory: $DAEMON_DIR"
[[ -d "$LOCK_DIR" ]] || die "Missing directory: $LOCK_DIR"
[[ -d "$SETTINGS_DIR" ]] || die "Missing directory: $SETTINGS_DIR"
[[ -d "$FILES_DIR" ]] || die "Missing directory: $FILES_DIR"
[[ -d "$TERMINAL_DIR" ]] || die "Missing directory: $TERMINAL_DIR"
[[ -d "$INSTALLER_DIR" ]] || die "Missing directory: $INSTALLER_DIR"

if [[ "$USE_SUDO" -eq 1 ]]; then
  need_cmd sudo
fi

if [[ "$ACTION" == "install" && "$LOGIN_MANAGER_CHOICE" != "none" ]]; then
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
build_project "$DAEMON_DIR"
build_project "$LOCK_DIR"
build_project "$SETTINGS_DIR"
build_project "$FILES_DIR"
build_project "$TERMINAL_DIR"
build_project "$INSTALLER_DIR"

if [[ "$ACTION" == "install" && "$SYSTEM_MODE" -eq 0 ]]; then
  migrate_legacy_config
  ensure_user_config
  warn_stage1_core_runtime_deps
  warn_monitor_runtime_deps
  warn_display_tweak_runtime_deps
  warn_screenshot_runtime_deps
  warn_portal_runtime_deps
fi

if [[ "$ACTION" == "install" ]]; then
  install_cursor_themes
  install_login_manager_packages
  setup_selected_login_manager
  switch_session_shell_to_dev
  restart_running_session

  log "Done."
  print_post_install_notes
else
  log "Build completed. No installation was performed."
fi
