#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src_dir="$script_dir/sources"

if ! command -v xcursorgen >/dev/null 2>&1; then
  echo "Uwaga: xcursorgen nie jest dostępny, pomijam budowanie własnych kursorów KartON" >&2
  exit 0
fi

build_cursor() {
  local theme="$1"
  local name="$2"
  local xhot="$3"
  local yhot="$4"
  local src_png="$5"

  local dst_dir="$script_dir/$theme/cursors"
  local cfg

  mkdir -p "$dst_dir"

  cfg="$(mktemp)"
  printf '24 %s %s %s 0\n' "$xhot" "$yhot" "$src_png" > "$cfg"
  xcursorgen "$cfg" "$dst_dir/$name"
  rm -f "$cfg"
}

link_aliases() {
  local theme="$1"
  local dst_dir="$script_dir/$theme/cursors"

  ln -sfn left_ptr "$dst_dir/default"
  ln -sfn left_ptr "$dst_dir/arrow"
  ln -sfn left_ptr "$dst_dir/top_left_arrow"
  ln -sfn hand2 "$dst_dir/pointer"
  ln -sfn hand2 "$dst_dir/pointing_hand"
  ln -sfn hand2 "$dst_dir/hand1"
  ln -sfn xterm "$dst_dir/text"
  ln -sfn watch "$dst_dir/progress"
  ln -sfn watch "$dst_dir/wait"
}

build_theme() {
  local theme="$1"
  local variant="$2"

  build_cursor "$theme" "left_ptr" 3 2 "$src_dir/left_ptr-$variant.png"
  build_cursor "$theme" "hand2" 9 3 "$src_dir/hand2-$variant.png"
  build_cursor "$theme" "xterm" 11 12 "$src_dir/xterm-$variant.png"
  build_cursor "$theme" "watch" 11 11 "$src_dir/watch-$variant.png"
  link_aliases "$theme"
}

build_theme "KartONCursorLight" "light"
build_theme "KartONCursorDark" "dark"

echo "OK: zbudowano kursory KartON (Light/Dark)"
