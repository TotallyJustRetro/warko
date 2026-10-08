#!/bin/sh
# Runs inside MSYS2 (MINGW64). Installs deps if missing, recompiles the ROM once, launches it.
set -e
cd "$GBHERE"
ROM=$(cygpath -u "$GBROM")
NAME=$(basename "$ROM" | sed 's/\.[^.]*$//; s/[^A-Za-z0-9_-]/_/g')
if ! command -v gcc >/dev/null || ! command -v sdl2-config >/dev/null || ! command -v python3 >/dev/null || ! command -v make >/dev/null; then
  echo "Installing build tools (one time)..."
  pacman -S --noconfirm --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python make
fi
OUT="build/$NAME"
if [ ! -f "$OUT/$NAME.exe" ]; then
  sh build.sh "$ROM" "$NAME"
fi
cp -f "$(dirname "$(command -v sdl2-config)")/SDL2.dll" "$OUT/" 2>/dev/null || true
exec "$OUT/$NAME.exe" "$ROM"
