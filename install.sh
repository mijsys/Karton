#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEKTURA_DIR="$SCRIPT_DIR/tektura"
SHELL_DIR="$SCRIPT_DIR/karton-shell"
SESSION_DIR="$SCRIPT_DIR/karton-session"
SETTINGS_DIR="$SCRIPT_DIR/karton-settings"

PREFIX="${HOME}/.local-karton"
USE_SUDO=0
SYSTEM_MODE=0
RESTART_SESSION=1
CONFIG_NAME="karton"

print_usage() {
  cat <<'EOF'
Usage: ./install.sh [options]

Builds and installs Karton compositor + shell + session components.

Options:
  --prefix <path>     Install prefix (default: ~/.local-karton)
  --system            Install system-wide to /usr/local (uses sudo)
  --no-restart        Do not restart running karton session modules
  -h, --help          Show this help

Examples:
  ./install.sh
  ./install.sh --prefix "$HOME/.local-karton"
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

build_and_install_project() {
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
  meson setup "$build_dir" "$src_dir" --reconfigure --prefix "$PREFIX" "${extra_setup_args[@]}"

  log "Building $(basename "$src_dir")"
  meson compile -C "$build_dir"

  log "Installing $(basename "$src_dir")"
  run_install "$build_dir"
}

ensure_user_config() {
  local config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/${CONFIG_NAME}"
  local autostart_file="$config_dir/autostart"
  local style_file="$config_dir/shell.css"
  local desktop_src="$PREFIX/share/applications/karton-settings.desktop"
  local desktop_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

  mkdir -p "$config_dir"

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

  local theme_sync_bin="$PREFIX/bin/karton-apply-theme"
  if [[ -x "$theme_sync_bin" ]]; then
    log "Syncing desktop theme/icon profile"
    "$theme_sync_bin" --sync-from-css || true
  fi

  if [[ -f "$desktop_src" ]]; then
    mkdir -p "$desktop_dir"
    cp "$desktop_src" "$desktop_dir/karton-settings.desktop"
  fi
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

restart_running_session() {
  if [[ "$RESTART_SESSION" -ne 1 ]]; then
    return
  fi

  if [[ "$EUID" -eq 0 ]]; then
    log "Skipping session restart (running as root)"
    return
  fi

  local autostart_file="${XDG_CONFIG_HOME:-$HOME/.config}/${CONFIG_NAME}/autostart"

  log "Restarting karton shell/session processes"
  pkill -f 'karton-shell --top-only' 2>/dev/null || true
  pkill -f 'karton-shell --side-only' 2>/dev/null || true
  pkill -x karton-shell 2>/dev/null || true
  pkill -f 'karton-sessiond' 2>/dev/null || true

  if [[ -x "$autostart_file" ]]; then
    "$autostart_file" || true
  else
    PATH="$PREFIX/bin:$PATH" karton-sessiond >/dev/null 2>&1 &
  fi

  sleep 1
  pgrep -fa 'karton-sessiond|karton-shell --top-only|karton-shell --side-only' || true
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      PREFIX="$2"
      shift 2
      ;;
    --system)
      SYSTEM_MODE=1
      PREFIX="/usr/local"
      USE_SUDO=1
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
      die "Unknown option: $1"
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

if [[ "$USE_SUDO" -eq 1 ]]; then
  need_cmd sudo
fi

if [[ "$SYSTEM_MODE" -eq 1 ]]; then
  log "Installing in system mode to $PREFIX"
  build_and_install_project "$TEKTURA_DIR" -Dsystemd-session=enabled
else
  log "Installing in user mode to $PREFIX"
  build_and_install_project "$TEKTURA_DIR" -Dsystemd-session=disabled
fi

build_and_install_project "$SHELL_DIR"
build_and_install_project "$SESSION_DIR"
build_and_install_project "$SETTINGS_DIR"

if [[ "$SYSTEM_MODE" -eq 0 ]]; then
  migrate_legacy_config
  ensure_user_config
  warn_screenshot_runtime_deps
fi

restart_running_session

log "Done."
if [[ "$SYSTEM_MODE" -eq 0 ]]; then
  cat <<EOF

Installed to: $PREFIX
Config dir:   ${XDG_CONFIG_HOME:-$HOME/.config}/${CONFIG_NAME}

If your display manager does not see the session, ensure XDG_DATA_DIRS includes:
  $PREFIX/share
EOF
fi
