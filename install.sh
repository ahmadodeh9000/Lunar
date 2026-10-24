#!/bin/sh
# Lunar installer: checks dependencies, builds, installs.
# Usage: ./install.sh [--sdl] [--prefix=DIR]
set -eu

SDL=0
PREFIX=""
for arg in "$@"; do
  case "$arg" in
    --sdl)       SDL=1 ;;
    --prefix=*)  PREFIX="${arg#--prefix=}" ;;
    -h|--help)   echo "usage: ./install.sh [--sdl] [--prefix=DIR]"; exit 0 ;;
    *)           echo "unknown option: $arg" >&2; exit 1 ;;
  esac
done

cd "$(dirname "$0")"
die() { echo "error: $*" >&2; exit 1; }

command -v make       >/dev/null 2>&1 || die "make not found"
command -v pkg-config >/dev/null 2>&1 || die "pkg-config not found"
command -v "${CC:-gcc}" >/dev/null 2>&1 || die "C compiler '${CC:-gcc}' not found"

if ! pkg-config --exists libffi; then
  cat >&2 <<MSG
error: libffi development files not found:
  Debian/Ubuntu: sudo apt install libffi-dev
  Fedora:        sudo dnf install libffi-devel
  Arch:          sudo pacman -S libffi
  macOS:         brew install libffi
MSG
  exit 1
fi

if [ "$SDL" -eq 1 ] && ! pkg-config --exists sdl2 SDL2_image; then
  cat >&2 <<MSG
error: SDL2 / SDL2_image development files not found:
  Debian/Ubuntu: sudo apt install libsdl2-dev libsdl2-image-dev
  Arch:          sudo pacman -S sdl2 sdl2_image
  macOS:         brew install sdl2 sdl2_image
MSG
  exit 1
fi

make clean
if [ "$SDL" -eq 1 ]; then make sdl; else make; fi

# sudo only if the install dir isn't writable by the current user
SUDO=""
if [ -n "$PREFIX" ]; then
  if mkdir -p "$PREFIX/bin" 2>/dev/null && [ -w "$PREFIX/bin" ]; then :; else SUDO="sudo"; fi
elif [ "$(id -u)" -ne 0 ]; then
  SUDO="sudo"
fi

$SUDO make install ${PREFIX:+PREFIX="$PREFIX"}
echo "Installed lunar."