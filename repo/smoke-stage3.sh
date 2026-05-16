#!/usr/bin/env bash
set -euo pipefail

ok=0
warn=0
fail=0

say_ok() { echo "[OK] $*"; ok=$((ok+1)); }
say_warn() { echo "[WARN] $*"; warn=$((warn+1)); }
say_fail() { echo "[FAIL] $*"; fail=$((fail+1)); }

check_cmd_required() {
  local cmd="$1"
  if command -v "$cmd" >/dev/null 2>&1; then
    say_ok "command available: $cmd"
  else
    say_fail "missing command: $cmd"
  fi
}

check_any_cmd() {
  local label="$1"
  shift
  local found=""
  local cmd

  for cmd in "$@"; do
    if command -v "$cmd" >/dev/null 2>&1; then
      found="$cmd"
      break
    fi
  done

  if [[ -n "$found" ]]; then
    say_ok "$label available: $found"
  else
    say_fail "$label missing (candidates: $*)"
  fi
}

check_default_mime_handler() {
  local mime="$1"
  local expected="$2"

  if ! command -v xdg-mime >/dev/null 2>&1; then
    say_warn "xdg-mime not available, skipping default handler checks"
    return 0
  fi

  local handler=""
  handler="$(xdg-mime query default "$mime" 2>/dev/null || true)"
  if [[ -n "$handler" ]]; then
    if [[ "$handler" == "$expected" ]]; then
      say_ok "default handler for $mime: $handler"
    else
      say_fail "default handler mismatch for $mime: got $handler, expected $expected"
    fi
  else
    say_warn "no default handler set for $mime"
  fi
}

echo "== Stage 3 smoke test (Faza C) =="

# Existing KartON baseline apps
check_cmd_required karton-files
check_cmd_required karton-terminal
check_cmd_required karton-settings

# MVP categories (can be KartON apps or external fallback apps)
check_any_cmd "image viewer" karton-images loupe imv eog gwenview ristretto
check_any_cmd "media player" karton-media mpv vlc totem parole
check_any_cmd "text editor" karton-text gedit mousepad kate xed pluma
check_any_cmd "pdf viewer" karton-pdf evince zathura okular atril

# Defaults integration baseline
check_default_mime_handler "image/png" "io.karton.Images.desktop"
check_default_mime_handler "video/mp4" "io.karton.Media.desktop"
check_default_mime_handler "text/plain" "io.karton.Text.desktop"
check_default_mime_handler "application/pdf" "io.karton.PDF.desktop"

echo
echo "Summary: OK=$ok WARN=$warn FAIL=$fail"

if [ "$fail" -gt 0 ]; then
  exit 1
fi

exit 0
