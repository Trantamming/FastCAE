#!/usr/bin/env bash
# Build a portable FastCAE AppImage (Linux x86_64).
# Needs: a Release build in ./build, plus linuxdeploy and its Qt plugin
# (https://github.com/linuxdeploy/linuxdeploy) available in $PATH or downloaded below.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
APPDIR="$ROOT/AppDir"
TOOLS="$ROOT/.appimage-tools"

rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

mkdir -p "$TOOLS"
if [ ! -x "$TOOLS/linuxdeploy-x86_64.AppImage" ]; then
  curl -L -o "$TOOLS/linuxdeploy-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  curl -L -o "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
  chmod +x "$TOOLS"/*.AppImage
fi

export PATH="$TOOLS:$PATH"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export VERSION="$(grep -m1 'VERSION' "$ROOT/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
export APPIMAGE_EXTRACT_AND_RUN=1   # works inside containers without FUSE

cd "$ROOT"
"$TOOLS/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --desktop-file "$ROOT/packaging/linux/fastcae.desktop" \
  --icon-file "$ROOT/resources/fastcae.svg" \
  --plugin qt \
  --output appimage

echo "Done: $(ls FastCAE*.AppImage)"
