#!/usr/bin/env bash
# Build and package ZipFX for Linux
# Requires: Qt 6, CMake, GCC/Clang, libfuse2 (for AppImage)
#
# Usage:  ./build_linux.sh [Qt_DIR]
#         default Qt_DIR = /usr  (system Qt)

set -euo pipefail

QT_DIR="${1:-}"
if [ -z "$QT_DIR" ]; then
    # Try to find Qt6 qmake automatically
    for candidate in \
        /home/krusher/Qt/6.*/gcc_64 \
        /opt/Qt/6.*/gcc_64 \
        /usr/lib/qt6 \
        /usr
    do
        # shellcheck disable=SC2086
        set -- $candidate  # expand glob
        if [ -x "${1}/bin/qmake" ] && "${1}/bin/qmake" --version 2>&1 | grep -q "Qt version 6"; then
            QT_DIR="$1"
            break
        fi
    done
    QT_DIR="${QT_DIR:-/usr}"
    echo "Auto-detected Qt: ${QT_DIR}"
fi
BUILD_DIR="build_linux"

echo "=== Configuring ${BUILD_DIR} ==="
cmake -S .. -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install/usr"

echo "=== Building ==="
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "=== Installing to prefix ==="
cmake --install "${BUILD_DIR}"

echo "=== Copying lib7z.so ==="
LIB7Z=""
for path in ../lib/linux/x64/lib7z.so ../lib/linux/lib7z.so; do
    if [ -f "$path" ]; then LIB7Z="$path"; break; fi
done
if [ -n "$LIB7Z" ]; then
    cp "$LIB7Z" "${BUILD_DIR}/install/usr/lib/"
fi

echo "=== Creating tar.gz ==="
tar czf ZipFX-Linux.tar.gz -C "${BUILD_DIR}/install/usr" .
echo "=== Done: ZipFX-Linux.tar.gz ==="

echo "=== Cleaning up non-ZipFX install artifacts ==="
find "${BUILD_DIR}/install/usr/bin" -type f ! -name "ZipFX" -delete
ln -sf ZipFX "${BUILD_DIR}/install/usr/bin/zipfx"
rm -rf "${BUILD_DIR}/install/usr/include" \
       "${BUILD_DIR}/install/usr/FALSE" \
       "${BUILD_DIR}/install/usr/share/man" \
       "${BUILD_DIR}/install/usr/lib/pkgconfig" \
       "${BUILD_DIR}/install/usr/lib/cmake"

echo "=== AppImage (if linuxdeploy available) ==="
if ! command -v linuxdeploy &>/dev/null; then
    echo "linuxdeploy not found — downloading..."
    curl -fsSL -o /tmp/linuxdeploy \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    curl -fsSL -o /tmp/linuxdeploy-plugin-qt \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x /tmp/linuxdeploy /tmp/linuxdeploy-plugin-qt
    export PATH="/tmp:$PATH"
fi
if command -v linuxdeploy &>/dev/null; then
    # Copy lib7z.so into the AppDir
    LIB7Z=""
    for path in ../lib/linux/x64/lib7z.so ../lib/linux/lib7z.so; do
        if [ -f "$path" ]; then LIB7Z="$path"; break; fi
    done
    mkdir -p "${BUILD_DIR}/install/usr/lib"
    if [ -n "$LIB7Z" ]; then
        cp "$LIB7Z" "${BUILD_DIR}/install/usr/lib/"
    fi
    # Copy shared dependency libs so linuxdeploy can bundle them
    find "${BUILD_DIR}/_deps" \( \
        -name "libzip.so*" -o -name "libstorm.so*" -o -name "liblzma.so*" \
        -o -name "libbrotli*.so*" \
        \) | xargs -I{} cp -P {} "${BUILD_DIR}/install/usr/lib/" 2>/dev/null || true
    export QMAKE="${QT_DIR}/bin/qmake"
    # Let linuxdeploy resolve shared libs from build tree and AppDir
    DEPS_LIB_DIRS=$(find "${BUILD_DIR}/_deps" -name "*.so*" -printf "%h\n" 2>/dev/null | sort -u | tr '\n' ':')
    export LD_LIBRARY_PATH="${BUILD_DIR}/install/usr/lib:${QT_DIR}/lib:${DEPS_LIB_DIRS}${LD_LIBRARY_PATH:-}"
    linuxdeploy --appdir "${BUILD_DIR}/install" \
        --desktop-file "${BUILD_DIR}/install/usr/share/applications/zipfx.desktop" \
        --icon-file "${BUILD_DIR}/install/usr/share/icons/hicolor/256x256/apps/zipfx.png" \
        --plugin qt --output appimage
    mv ZipFX*.AppImage ZipFX-Linux.AppImage 2>/dev/null || true
    echo "=== AppImage created ==="
else
    echo "=== Skipping AppImage (linuxdeploy not found) ==="
fi
