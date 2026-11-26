#!/usr/bin/env bash
# Build the artemis-hyprland pacman package in a clean Arch Linux environment.
#
# Runs as root inside a disposable archlinux:base-devel container: the GitHub
# Actions job container, or locally (see README.md). The source tree is copied
# into the container first, so the checkout is never modified or re-owned.
#
# Usage: build-in-container.sh <output-dir>
# Env:   ARTEMIS_PKGVER  package version (default: app/version.txt + git revision)
#        ARTEMIS_PKGREL  package release number (default: 1)
#
# When run under GitHub Actions, writes pkgver and artifact to $GITHUB_OUTPUT.
set -euo pipefail

src=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
out=$(realpath -m "${1:?usage: $0 <output-dir>}")
pkgrel=${ARTEMIS_PKGREL:-1}

if [[ $(id -u) -ne 0 ]]; then
    echo "error: run this as root inside a disposable container" >&2
    exit 1
fi
if [[ ! $pkgrel =~ ^[1-9][0-9]*$ ]]; then
    echo "error: ARTEMIS_PKGREL must be a positive integer, got '$pkgrel'" >&2
    exit 1
fi

pacman -Syu --noconfirm --needed git

# makepkg refuses to run as root
id -u builder &>/dev/null || useradd --create-home builder
as_builder() { runuser -u builder -- "$@"; }

work=/home/builder/artemis
rm -rf "$work"
cp -a "$src" "$work"
chown -R builder: "$work"

# The Linux build needs these submodules. The large "libs" submodule only holds
# Windows and macOS prebuilts, so it's skipped.
as_builder git -C "$work" submodule update --init --recursive --depth 1 -- \
    moonlight-common-c/moonlight-common-c \
    qmdnsengine/qmdnsengine \
    h264bitstream/h264bitstream \
    soundio/libsoundio \
    app/SDL_GameControllerDB

cd "$work/packaging/arch"

mapfile -t deps < <(bash -c 'source ./PKGBUILD && printf "%s\n" "${depends[@]}" "${makedepends[@]}"')
pacman -S --noconfirm --needed "${deps[@]}"

if [[ -n ${ARTEMIS_PKGVER:-} ]]; then
    pkgver=$ARTEMIS_PKGVER
    appver=$(tr -d '\r\n' < "$work/app/version.txt")
    if [[ $pkgver != "$appver"* ]]; then
        echo "::warning::Package version $pkgver doesn't match app/version.txt ($appver); the app will report $appver"
    fi
else
    # shellcheck disable=SC2016 # expanded by the inner shell
    pkgver=$(as_builder bash -c 'startdir=$PWD && source ./PKGBUILD && pkgver')
fi
# Same rule makepkg applies (no hyphens, colons or whitespace)
if [[ ! $pkgver =~ ^[A-Za-z0-9._+]+$ ]]; then
    echo "error: invalid package version '$pkgver'" >&2
    exit 1
fi

# Stamp the version so makepkg's pkgver() returns the same value and doesn't
# reset pkgrel when it rewrites the PKGBUILD.
sed -i -e "s/^pkgver=.*/pkgver=$pkgver/" -e "s/^pkgrel=.*/pkgrel=$pkgrel/" PKGBUILD

as_builder env ARTEMIS_PKGVER="$pkgver" MAKEFLAGS="-j$(nproc)" \
    makepkg --noconfirm --cleanbuild --force

mkdir -p "$out"
shopt -s nullglob
packages=(./*.pkg.tar.zst)
if (( ${#packages[@]} == 0 )); then
    echo "error: makepkg produced no packages" >&2
    exit 1
fi
cp -- "${packages[@]}" "$out/"

echo "Built packages in $out:"
ls -l "$out"

if [[ -n ${GITHUB_OUTPUT:-} ]]; then
    {
        echo "pkgver=$pkgver-$pkgrel"
        echo "artifact=artemis-hyprland-$pkgver-$pkgrel-x86_64"
    } >> "$GITHUB_OUTPUT"
fi
