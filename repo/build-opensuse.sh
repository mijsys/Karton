#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

sudo zypper install -y \
    gcc gcc-c++ make meson ninja pkgconf-pkg-config wayland-devel \
    wayland-protocols-devel wlroots-devel cairo-devel pango-devel \
    glib2-devel gtk3-devel gtk4-devel gdk-pixbuf-devel libpulse-devel \
    libxkbcommon-devel libinput-devel libxml2-devel pixman-devel scdoc

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
