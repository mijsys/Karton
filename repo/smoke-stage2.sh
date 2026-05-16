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

check_cmd_optional() {
  local cmd="$1"
  if command -v "$cmd" >/dev/null 2>&1; then
    say_ok "optional command available: $cmd"
  else
    say_warn "optional command missing: $cmd"
  fi
}

has_polkit_agent_binary() {
  command -v /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 \
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
    || command -v mate-polkit >/dev/null 2>&1
}

has_portal_backend_binary() {
  command -v xdg-desktop-portal-wlr >/dev/null 2>&1 \
    || command -v xdg-desktop-portal-gtk >/dev/null 2>&1 \
    || command -v xdg-desktop-portal-gnome >/dev/null 2>&1 \
    || [ -x /usr/lib/xdg-desktop-portal-wlr ] \
    || [ -x /usr/libexec/xdg-desktop-portal-wlr ] \
    || [ -x /usr/lib/xdg-desktop-portal-gtk ] \
    || [ -x /usr/libexec/xdg-desktop-portal-gtk ] \
    || [ -x /usr/lib/xdg-desktop-portal-gnome ] \
    || [ -x /usr/libexec/xdg-desktop-portal-gnome ]
}

check_runtime_process() {
  local pattern="$1"
  local label="$2"

  if pgrep -af "$pattern" >/dev/null 2>&1; then
    say_ok "runtime process present: $label"
  else
    say_fail "runtime process missing: $label"
  fi
}

check_portal_interface() {
  local iface="$1"

  if gdbus introspect --session \
    --dest org.freedesktop.portal.Desktop \
    --object-path /org/freedesktop/portal/desktop 2>/dev/null | grep -F "interface $iface" >/dev/null 2>&1; then
    say_ok "portal interface available: $iface"
  else
    say_fail "portal interface missing: $iface"
  fi
}

echo "== Stage 2 smoke test (Faza B) =="

# Lockscreen + idle integration binaries
check_cmd_required karton-lock
check_cmd_required karton-idle
check_cmd_required karton-settingsd
check_cmd_required karton-settings

# Polkit integration baseline
if has_polkit_agent_binary; then
  say_ok "polkit agent candidate found"
else
  say_fail "no supported polkit agent candidate found"
fi

check_cmd_required pkexec

if command -v pkaction >/dev/null 2>&1; then
  if pkaction --version >/dev/null 2>&1; then
    say_ok "polkit tooling reachable (pkaction)"
  else
    say_warn "pkaction present but not responding"
  fi
else
  say_warn "pkaction not available (cannot verify local actions list)"
fi

# Portal integration baseline
if command -v xdg-desktop-portal >/dev/null 2>&1 \
  || [ -x /usr/lib/xdg-desktop-portal ] \
  || [ -x /usr/libexec/xdg-desktop-portal ]; then
  say_ok "xdg-desktop-portal found"
else
  say_fail "xdg-desktop-portal missing"
fi

if has_portal_backend_binary; then
  say_ok "portal backend found (wlr/gtk/gnome)"
else
  say_fail "no portal backend found (wlr/gtk/gnome)"
fi

check_cmd_optional gdbus

# Optional lock-now probe (can be invasive)
if [[ "${KARTON_SMOKE_STAGE2_NO_LOCK_TEST:-1}" == "1" ]]; then
  say_warn "pomijam test karton-settingsd --lock-now (KARTON_SMOKE_STAGE2_NO_LOCK_TEST=1)"
else
  if karton-settingsd --lock-now >/dev/null 2>&1; then
    say_ok "karton-settingsd --lock-now returns success"
  else
    say_fail "karton-settingsd --lock-now failed"
  fi
fi

# Runtime checks require an active user session bus.
if [[ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
  check_runtime_process "xdg-desktop-portal($| )" "xdg-desktop-portal"
  check_runtime_process "xdg-desktop-portal-(wlr|gtk|gnome)($| )" "xdg-desktop-portal backend"
  check_runtime_process "polkit.*agent|lxqt-policykit-agent|mate-polkit" "polkit agent"

  if command -v gdbus >/dev/null 2>&1; then
    if gdbus call --session \
      --dest org.freedesktop.portal.Desktop \
      --object-path /org/freedesktop/portal/desktop \
      --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
      say_ok "portal D-Bus endpoint reachable"

      # Faza B: API potrzebne do chooser/screenshot/screen-share.
      check_portal_interface "org.freedesktop.portal.FileChooser"
      check_portal_interface "org.freedesktop.portal.Screenshot"
      check_portal_interface "org.freedesktop.portal.ScreenCast"
    else
      say_fail "portal D-Bus endpoint unreachable"
    fi
  fi
else
  say_warn "brak DBUS_SESSION_BUS_ADDRESS, pomijam runtime testy procesow i D-Bus"
fi

echo
echo "Summary: OK=$ok WARN=$warn FAIL=$fail"

if [ "$fail" -gt 0 ]; then
  exit 1
fi

exit 0
