#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SESSION_DIR="$ROOT_DIR/karton-session"

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

check_cmd_optional() {
  local cmd="$1"
  if command -v "$cmd" >/dev/null 2>&1; then
    say_ok "optional command available: $cmd"
  else
    say_warn "optional command missing: $cmd"
  fi
}

echo "== Stage 1 smoke test =="

# Core KartON binaries expected after install
check_cmd_required karton-sessiond
check_cmd_required karton-settingsd
check_cmd_required karton-shell
check_cmd_required karton-settings
check_cmd_required karton-files
check_cmd_required karton-terminal
check_cmd_required karton-lock
check_cmd_required karton-idle

# 1.1 Lockscreen + idle
check_cmd_required swaylock
check_cmd_required swayidle
check_cmd_required wlopm
check_cmd_optional kanshi

# 1.2 Polkit agent candidates
if command -v /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/libexec/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/bin/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
  || command -v polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/lib/polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/libexec/polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/bin/polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
  || command -v polkit-kde-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/lib/lxqt-policykit-agent/lxqt-policykit-agent >/dev/null 2>&1 \
  || command -v /usr/libexec/lxqt-policykit-agent >/dev/null 2>&1 \
  || command -v /usr/bin/lxqt-policykit-agent >/dev/null 2>&1 \
  || command -v lxqt-policykit-agent >/dev/null 2>&1 \
  || command -v /usr/lib/mate-polkit/polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/libexec/polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
  || command -v /usr/bin/polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
  || command -v polkit-mate-authentication-agent-1 >/dev/null 2>&1 \
  || command -v mate-polkit >/dev/null 2>&1; then
  say_ok "polkit agent candidate found"
else
  say_fail "no supported polkit agent candidate found"
fi

# 1.3 Portal core + backend
if command -v xdg-desktop-portal >/dev/null 2>&1 \
  || [ -x /usr/lib/xdg-desktop-portal ] \
  || [ -x /usr/libexec/xdg-desktop-portal ]; then
  say_ok "xdg-desktop-portal found"
else
  say_fail "xdg-desktop-portal missing"
fi

if command -v xdg-desktop-portal-wlr >/dev/null 2>&1 \
  || command -v xdg-desktop-portal-gtk >/dev/null 2>&1 \
  || command -v xdg-desktop-portal-gnome >/dev/null 2>&1 \
  || [ -x /usr/lib/xdg-desktop-portal-wlr ] \
  || [ -x /usr/libexec/xdg-desktop-portal-wlr ] \
  || [ -x /usr/lib/xdg-desktop-portal-gtk ] \
  || [ -x /usr/libexec/xdg-desktop-portal-gtk ] \
  || [ -x /usr/lib/xdg-desktop-portal-gnome ] \
  || [ -x /usr/libexec/xdg-desktop-portal-gnome ]; then
  say_ok "portal backend found (wlr/gtk/gnome)"
else
  say_fail "no portal backend found (wlr/gtk/gnome)"
fi

# 1.4 Notifications + clipboard
check_cmd_required wl-paste
check_cmd_required cliphist
check_cmd_optional notify-send

# Script-level checks
if sh -n "$SESSION_DIR/bin/karton-sessiond"; then
  say_ok "karton-sessiond syntax OK"
else
  say_fail "karton-sessiond syntax error"
fi

if sh -n "$SESSION_DIR/bin/karton-settingsd"; then
  say_ok "karton-settingsd syntax OK"
else
  say_fail "karton-settingsd syntax error"
fi

if [[ "${KARTON_SMOKE_NO_LOCK_TEST:-0}" == "1" ]]; then
  say_warn "pomijam test karton-settingsd --lock-now (KARTON_SMOKE_NO_LOCK_TEST=1)"
else
  if sh "$SESSION_DIR/bin/karton-settingsd" --lock-now >/dev/null 2>&1; then
    say_ok "karton-settingsd --lock-now returns success"
  else
    say_fail "karton-settingsd --lock-now failed"
  fi
fi

echo
echo "Summary: OK=$ok WARN=$warn FAIL=$fail"

if [ "$fail" -gt 0 ]; then
  exit 1
fi

exit 0
