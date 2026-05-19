#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${HOME}/.local-karton"
RESTART_SESSION=1
PASS_ARGS=()

print_usage() {
  cat <<'EOF'
Usage: ./install-dev.sh [options] [install.sh options]

Build, install, and then switch the running Karton session to the development
karton-shell binary from karton-shell/builddir-user.

Options:
  --prefix <path>     Install prefix (default: ~/.local-karton)
  --no-restart        Do not restart running karton session modules after switch
  -h, --help          Show this help

All remaining arguments are forwarded to ./install.sh.

Examples:
  ./install-dev.sh
  ./install-dev.sh --prefix "$HOME/.local-karton"
  ./install-dev.sh --no-restart
EOF
}

log() {
  printf '[install-dev] %s\n' "$*"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      PREFIX="$2"
      PASS_ARGS+=("$1" "$2")
      shift 2
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
      PASS_ARGS+=("$1")
      shift
      ;;
  esac
done

INSTALL_SH="$SCRIPT_DIR/install.sh"
[[ -x "$INSTALL_SH" ]] || {
  printf '[install-dev][error] Missing installer: %s\n' "$INSTALL_SH" >&2
  exit 1
}

log "Running install.sh in dev-shell mode"
if [[ "$RESTART_SESSION" -eq 1 ]]; then
  exec "$INSTALL_SH" --dev-shell "${PASS_ARGS[@]}"
else
  exec "$INSTALL_SH" --dev-shell --no-restart "${PASS_ARGS[@]}"
fi