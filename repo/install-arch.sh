#!/usr/bin/env bash
set -euo pipefail

repo_url="${KARTON_REPO_URL:-https://github.com/mijsys/Tektura-i-Karton.git}"
workdir="${KARTON_WORKDIR:-$HOME/.cache/karton-src}"
install_mode="${KARTON_INSTALL_MODE:-erase}"
target_disk="${KARTON_TARGET_DISK:-}"
hostname_value="${KARTON_HOSTNAME:-karton}"
timezone_value="${KARTON_TIMEZONE:-Europe/Warsaw}"
username_value="${KARTON_USERNAME:-karton}"
fullname_value="${KARTON_FULLNAME:-Karton User}"
dry_run="${KARTON_DRY_RUN:-0}"

if [[ "$install_mode" != "erase" && "$install_mode" != "manual" ]]; then
	echo "Nieobslugiwany KARTON_INSTALL_MODE=$install_mode (dozwolone: erase/manual)" >&2
	exit 1
fi

if [[ "$install_mode" == "erase" && -z "$target_disk" ]]; then
	echo "Tryb erase wymaga KARTON_TARGET_DISK=/dev/..." >&2
	exit 1
fi

if [[ -n "$target_disk" && ! -b "$target_disk" ]]; then
	echo "KARTON_TARGET_DISK nie wskazuje na urzadzenie blokowe: $target_disk" >&2
	exit 1
fi

echo "[karton-install] mode=$install_mode disk=${target_disk:-none} dry_run=$dry_run"
echo "[karton-install] host=$hostname_value tz=$timezone_value user=$username_value"

if [[ "$dry_run" == "1" ]]; then
	echo "[karton-install] Dry run aktywny: pomijam klonowanie i uruchamianie build-arch.sh"
	exit 0
fi

rm -rf "$workdir"
git clone --depth 1 "$repo_url" "$workdir"

export KARTON_INSTALL_MODE="$install_mode"
export KARTON_TARGET_DISK="$target_disk"
export KARTON_HOSTNAME="$hostname_value"
export KARTON_TIMEZONE="$timezone_value"
export KARTON_USERNAME="$username_value"
export KARTON_FULLNAME="$fullname_value"

if [[ "$install_mode" == "erase" ]]; then
	export KARTON_PARTITION_PROFILE="auto-erase"
else
	export KARTON_PARTITION_PROFILE="manual"
fi

exec bash "$workdir/repo/build-arch.sh"