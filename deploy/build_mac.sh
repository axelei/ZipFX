#!/usr/bin/env bash
# Build and package ZipFX for macOS
# Requires: Qt 6, CMake, Xcode command-line tools
#
# Usage:  ./build_mac.sh [Qt_DIR]
#         default Qt_DIR = /usr/local/opt/qt  (Homebrew)

set -euo pipefail

QT_DIR="${1:-/usr/local/opt/qt}"
BUILD_DIR="build_mac"

echo "=== Configuring ${BUILD_DIR} ==="
cmake -S .. -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

echo "=== Building ==="
cmake --build "${BUILD_DIR}" --target ZipFX -j"$(sysctl -n hw.logicalcpu)"

echo "=== Running macdeployqt ==="
if command -v macdeployqt &>/dev/null; then
    macdeployqt "${BUILD_DIR}/ZipFX.app" -verbose=1
else
    echo "Warning: macdeployqt not found — searching in Qt_DIR"
    "${QT_DIR}/bin/macdeployqt" "${BUILD_DIR}/ZipFX.app" -verbose=1
fi

echo "=== Copying lib7z.so ==="
LIB7Z=""
for path in ../lib/macos/x64/lib7z.so ../lib/macos/lib7z.so; do
    if [ -f "$path" ]; then LIB7Z="$path"; break; fi
done
if [ -n "$LIB7Z" ]; then
    cp "$LIB7Z" "${BUILD_DIR}/ZipFX.app/Contents/MacOS/"
fi

echo "=== Creating DMG ==="
PKG_NAME="ZipFX-macOS.dmg"
if command -v create-dmg &>/dev/null; then
    create-dmg \
        --volname "ZipFX" \
        --window-pos 200 120 \
        --window-size 600 450 \
        --icon-size 100 \
        --icon "ZipFX.app" 175 120 \
        --hide-extension "ZipFX.app" \
        --app-drop-link 425 120 \
        "${PKG_NAME}" \
        "${BUILD_DIR}/ZipFX.app"
else
    hdiutil create -volname "ZipFX" -srcfolder "${BUILD_DIR}/ZipFX.app" \
        -ov -format UDZO "${PKG_NAME}"
fi

echo "=== Done: ${PKG_NAME} ==="
