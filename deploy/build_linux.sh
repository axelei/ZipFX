#!/usr/bin/env bash
# Build and package ZipFX for Linux
# Requires: Qt 6, CMake, GCC/Clang, libfuse2 (for AppImage), libfuse3-dev (optional, for lazy drag)
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
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install/usr" \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib'

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

echo "=== Copying AppIcon.png + LICENSE next to executable ==="
cp ../src/resources/AppIcon.png "${BUILD_DIR}/install/usr/bin/"
cp ../LICENSE "${BUILD_DIR}/install/usr/bin/"

echo "=== Bundling system shared libs ==="
mkdir -p "${BUILD_DIR}/install/usr/lib"
# libfuse3 — needed for lazy drag-and-drop; locate via ldconfig then copy with symlinks
LIBFUSE3_REAL=$(ldconfig -p 2>/dev/null | awk '/libfuse3\.so/{print $NF}' | head -1)
if [ -n "$LIBFUSE3_REAL" ]; then
    LIBFUSE3_DIR=$(dirname "$LIBFUSE3_REAL")
    find "$LIBFUSE3_DIR" -maxdepth 1 -name "libfuse3.so*" \
        -exec cp -Pn {} "${BUILD_DIR}/install/usr/lib/" \; 2>/dev/null || true
    echo "Bundled libfuse3 from ${LIBFUSE3_DIR}"
else
    echo "libfuse3 not found — lazy drag-and-drop will fall back to eager extraction"
fi

echo "=== Creating tar.gz ==="
tar czf ZipFX-Linux.tar.gz -C "${BUILD_DIR}/install/usr" .
echo "=== Done: ZipFX-Linux.tar.gz ==="

echo "=== Cleaning up non-ZipFX install artifacts ==="
find "${BUILD_DIR}/install/usr/bin" -type f ! -name "ZipFX" ! -name "AppIcon.png" ! -name "LICENSE" -delete
ln -sf ZipFX "${BUILD_DIR}/install/usr/bin/zipfx"
rm -rf "${BUILD_DIR}/install/usr/include" \
       "${BUILD_DIR}/install/usr/FALSE" \
       "${BUILD_DIR}/install/usr/share/man" \
       "${BUILD_DIR}/install/usr/lib/pkgconfig" \
       "${BUILD_DIR}/install/usr/lib/cmake"

echo "=== AppImage (if linuxdeploy available) ==="
# Reuse a previously downloaded linuxdeploy if present and executable, so
# repeated builds don't re-download and curl doesn't try to overwrite a live file.
[ -x /tmp/linuxdeploy ] && export PATH="/tmp:$PATH"
if ! command -v linuxdeploy &>/dev/null; then
    echo "linuxdeploy not found — downloading..."
    curl -fsSL -o /tmp/linuxdeploy \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    curl -fsSL -o /tmp/linuxdeploy-plugin-qt \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x /tmp/linuxdeploy /tmp/linuxdeploy-plugin-qt
    export PATH="/tmp:$PATH"
fi
# linuxdeploy and appimagetool are themselves AppImages; without this they try
# to FUSE-mount themselves, which hangs or fails in containers and CI.
export APPIMAGE_EXTRACT_AND_RUN=1
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
    # libfuse3 is a system lib (not in _deps); copy it if not already present from the tar.gz step
    LIBFUSE3_REAL=$(ldconfig -p 2>/dev/null | awk '/libfuse3\.so/{print $NF}' | head -1)
    if [ -n "$LIBFUSE3_REAL" ]; then
        find "$(dirname "$LIBFUSE3_REAL")" -maxdepth 1 -name "libfuse3.so*" \
            -exec cp -Pn {} "${BUILD_DIR}/install/usr/lib/" \; 2>/dev/null || true
    fi
    export QMAKE="${QT_DIR}/bin/qmake"
    # Let linuxdeploy resolve shared libs from build tree and AppDir
    DEPS_LIB_DIRS=$(find "${BUILD_DIR}/_deps" -name "*.so*" -printf "%h\n" 2>/dev/null | sort -u | tr '\n' ':')
    export LD_LIBRARY_PATH="${BUILD_DIR}/install/usr/lib:${QT_DIR}/lib:${DEPS_LIB_DIRS}${LD_LIBRARY_PATH:-}"
    # Bundle the Wayland QPA plugin so Qt runs natively under Wayland
    # (drag-and-drop is broken under XWayland on modern compositors).
    WAYLAND_PLUGIN="${QT_DIR}/plugins/platforms/libqwayland.so"
    if [ -f "$WAYLAND_PLUGIN" ]; then
        mkdir -p "${BUILD_DIR}/install/usr/plugins/platforms"
        cp "$WAYLAND_PLUGIN" "${BUILD_DIR}/install/usr/plugins/platforms/"
        # Also bundle libQt6WaylandClient (so dependency)
        for lib in "${QT_DIR}/lib/libQt6WaylandClient.so"*; do
            [ -f "$lib" ] && cp -P "$lib" "${BUILD_DIR}/install/usr/lib/"
        done
        # Shell integration (xdg-shell) — required for the Wayland plugin to work
        WAYLAND_SHELL="${QT_DIR}/plugins/wayland-shell-integration"
        if [ -d "$WAYLAND_SHELL" ]; then
            mkdir -p "${BUILD_DIR}/install/usr/plugins/wayland-shell-integration"
            cp "$WAYLAND_SHELL"/*.so "${BUILD_DIR}/install/usr/plugins/wayland-shell-integration/" 2>/dev/null || true
        fi
        # Graphics integration (wayland-egl) — required for rendering
        WAYLAND_GFX="${QT_DIR}/plugins/wayland-graphics-integration-client"
        if [ -d "$WAYLAND_GFX" ]; then
            mkdir -p "${BUILD_DIR}/install/usr/plugins/wayland-graphics-integration-client"
            cp "$WAYLAND_GFX"/*.so "${BUILD_DIR}/install/usr/plugins/wayland-graphics-integration-client/" 2>/dev/null || true
            # libQt6OpenGL is a transitive dependency of the egl integration
            for lib in "${QT_DIR}/lib/libQt6OpenGL.so"*; do
                [ -f "$lib" ] && cp -P "$lib" "${BUILD_DIR}/install/usr/lib/"
            done
        fi
        # Decoration client (bradient/adwaita) — optional, for server-side decorations
        WAYLAND_DECOR="${QT_DIR}/plugins/wayland-decoration-client"
        if [ -d "$WAYLAND_DECOR" ]; then
            mkdir -p "${BUILD_DIR}/install/usr/plugins/wayland-decoration-client"
            cp "$WAYLAND_DECOR"/*.so "${BUILD_DIR}/install/usr/plugins/wayland-decoration-client/" 2>/dev/null || true
        fi
        echo "Bundled Wayland support from ${QT_DIR}"
    else
        echo "Wayland QPA plugin not found at ${WAYLAND_PLUGIN} — XWayland fallback"
    fi
    # Remove stale AppImages before building so the glob below is unambiguous.
    rm -f ZipFX*.AppImage
    # Fill the AppDir with all libraries and plugins.
    linuxdeploy --appdir "${BUILD_DIR}/install" \
        --desktop-file "${BUILD_DIR}/install/usr/share/applications/zipfx.desktop" \
        --icon-file "${BUILD_DIR}/install/usr/share/icons/hicolor/256x256/apps/zipfx.png" \
        --plugin qt

    # Remove bundled glib — it's a universal system library and bundling it
    # pulls in a GLIBC version requirement from the build host
    # (e.g. GLIBC_2.43 on Ubuntu 26.04) that breaks on older distros.
    rm -f "${BUILD_DIR}/install/usr/lib/libglib-2.0.so"*

    # Create the AppImage via appimagetool (extracted from linuxdeploy).
    # We cannot use linuxdeploy --output appimage here because it would
    # re-deploy the glib library we just removed.
    if [ ! -x /tmp/appimagetool ]; then
        (cd /tmp && /tmp/linuxdeploy --appimage-extract 1>/dev/null 2>&1)
        if [ -f /tmp/squashfs-root/plugins/linuxdeploy-plugin-appimage/appimagetool-prefix/usr/bin/appimagetool ]; then
            cp /tmp/squashfs-root/plugins/linuxdeploy-plugin-appimage/appimagetool-prefix/usr/bin/appimagetool /tmp/appimagetool
            chmod +x /tmp/appimagetool
            rm -rf /tmp/squashfs-root
        fi
    fi
    if [ -x /tmp/appimagetool ]; then
        VERSION="${VERSION:-continuous}"
        ARCH="${ARCH:-x86_64}"
        /tmp/appimagetool --no-appstream "${BUILD_DIR}/install" "ZipFX-Linux.AppImage" 2>&1
    else
        echo "=== appimagetool not available — skipping AppImage ==="
    fi
    mv ZipFX*.AppImage ZipFX-Linux.AppImage 2>/dev/null || true
    echo "=== AppImage created ==="
else
    echo "=== Skipping AppImage (linuxdeploy not found) ==="
fi
