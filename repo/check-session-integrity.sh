#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

desktop_file="$project_root/tektura/data/karton.desktop"
wrapper_file="$project_root/tektura/data/karton-session-start"
meson_file="$project_root/tektura/meson.build"
arch_pkgbuild="$project_root/repo/archlinux/PKGBUILD"

fail() {
  echo "[session-check][FAIL] $*" >&2
  exit 1
}

ok() {
  echo "[session-check][OK] $*"
}

[[ -f "$desktop_file" ]] || fail "missing session desktop file: $desktop_file"
[[ -f "$wrapper_file" ]] || fail "missing session wrapper: $wrapper_file"
[[ -f "$meson_file" ]] || fail "missing meson file: $meson_file"
[[ -f "$arch_pkgbuild" ]] || fail "missing Arch PKGBUILD: $arch_pkgbuild"

if ! grep -q '^Exec=/usr/bin/karton-session-start$' "$desktop_file"; then
  fail "karton.desktop must use Exec=/usr/bin/karton-session-start"
fi
ok "karton.desktop Exec is stable"

if grep -q 'karton-sessiond' "$wrapper_file"; then
  fail "karton-session-start must not start karton-sessiond directly (avoids duplicate panels)"
fi
ok "wrapper does not duplicate karton-sessiond startup"

if ! grep -q 'XDG_DATA_DIRS=' "$wrapper_file"; then
  fail "karton-session-start should export XDG_DATA_DIRS for app icons/desktop entries"
fi
ok "wrapper exports XDG_DATA_DIRS"

if ! sh -n "$wrapper_file"; then
  fail "karton-session-start has shell syntax errors"
fi
ok "karton-session-start syntax is valid"

if ! grep -q "install_data('data/karton-session-start'" "$meson_file"; then
  fail "tektura/meson.build does not install karton-session-start"
fi
ok "meson installs karton-session-start"

if ! grep -q "'lxqt-policykit'" "$arch_pkgbuild"; then
  fail "Arch PKGBUILD metapackage should depend on lxqt-policykit"
fi
ok "Arch PKGBUILD depends on polkit agent"

echo "[session-check] all checks passed"
