#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 mijsys
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential meson ninja-build pkg-config wayland-protocols \
    libwayland-dev libwlroots-dev libcairo2-dev libpango1.0-dev \
    libglib2.0-dev libgtk-3-dev libxkbcommon-dev libinput-dev \
    libxml2-dev libpixman-1-dev scdoc

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"
build_all_karton
