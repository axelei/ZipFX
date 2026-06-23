#!/usr/bin/env bash
# Build and package ZipFX for Linux
# Requires: Qt 6, CMake, GCC/Clang, libfuse2 (for AppImage)
#
# Usage:  ./build_linux.sh [Qt_DIR]
#         default Qt_DIR = /usr  (system Qt)

set -euo pipefail

QT_DIR="${1:-/usr}"
BUILD_DIR="build_linux"

echo "=== Configuring ${BUILD_DIR} ==="
cmake -S .. -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"

echo "=== Building ==="
cmake --build "${BUILD_DIR}" --target ZipFX -j"$(nproc)"

echo "=== Installing to prefix ==="
cmake --install "${BUILD_DIR}"

echo "=== Copying lib7z.so ==="
LIB7Z=""
for path in ../lib/linux/x64/lib7z.so ../lib/linux/lib7z.so; do
    if [ -f "$path" ]; then LIB7Z="$path"; break; fi
done
if [ -n "$LIB7Z" ]; then
    cp "$LIB7Z" "${BUILD_DIR}/install/bin/"
fi

echo "=== AppImage (if linuxdeploy available) ==="
if command -v linuxdeployqt &>/dev/null; then
    linuxdeployqt "${BUILD_DIR}/install/share/applications/zipfx.desktop" \
        -appimage -bundle-non-qt-libs
    mv ZipFX*.AppImage ZipFX-Linux.AppImage 2>/dev/null || true
    echo "=== AppImage created ==="
else
    echo "=== Creating tar.gz (linuxdeployqt not found) ==="
    tar czf ZipFX-Linux.tar.gz -C "${BUILD_DIR}/install" .
    echo "=== Done: ZipFX-Linux.tar.gz ==="
fi
