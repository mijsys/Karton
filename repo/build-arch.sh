#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

sudo pacman -S --needed --noconfirm \
    base-devel meson ninja pkgconf wayland wayland-protocols wlroots \
    cairo pango glib2 gtk3 libxkbcommon libinput libxml2 pixman scdoc

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
