#!/usr/bin/env bash
# Build and package ZipFX for macOS
# Requires: Qt 6, CMake, Xcode command-line tools
#
# Usage:  ./build_mac.sh [Qt_DIR]
#         default Qt_DIR = /usr/local/opt/qt  (Homebrew)

set -euo pipefail

QT_DIR="${1:-${HOME}/Qt/6.11.1/macos}"
BUILD_DIR="build_mac"

# Find cmake (CLion bundles it)
CMAKE=""
for cmd in cmake /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake /usr/local/bin/cmake /opt/homebrew/bin/cmake; do
    if command -v "$cmd" &>/dev/null; then CMAKE="$cmd"; break; fi
done
if [ -z "$CMAKE" ]; then echo "cmake not found"; exit 1; fi

echo "=== Configuring ${BUILD_DIR} ==="
"${CMAKE}" -S .. -B "${BUILD_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_TESTING=OFF

echo "=== Building ==="
"${CMAKE}" --build "${BUILD_DIR}" --target ZipFX -j"$(sysctl -n hw.logicalcpu)"

echo "=== Bundling storm.framework ==="
# macdeployqt cannot resolve @rpath references pointing at CMake build dirs,
# and it also does not expand @executable_path when building its rpath search
# list — so leaving the reference as @rpath/storm... causes the ERROR even
# after the framework is copied into the bundle.
# Fix: copy the framework, rewrite the app's LC_LOAD_DYLIB entry and the
# framework's own install name to @executable_path/../Frameworks/..., and
# remove the dangling absolute build-dir rpath.  macdeployqt skips
# @executable_path references entirely (treats them as already bundled).
STORM_SRC="${BUILD_DIR}/_deps/stormlib-build/storm.framework"
FRAMEWORKS_DIR="${BUILD_DIR}/ZipFX.app/Contents/Frameworks"
APP_BIN="${BUILD_DIR}/ZipFX.app/Contents/MacOS/ZipFX"
STORM_RPATH="@rpath/storm.framework/Versions/9.30.0/storm"
STORM_BUNDLED="@executable_path/../Frameworks/storm.framework/Versions/9.30.0/storm"
if [ -d "$STORM_SRC" ]; then
    mkdir -p "$FRAMEWORKS_DIR"
    cp -R "$STORM_SRC" "$FRAMEWORKS_DIR/"
    install_name_tool -id "$STORM_BUNDLED" \
        "$FRAMEWORKS_DIR/storm.framework/Versions/9.30.0/storm"
    install_name_tool -change "$STORM_RPATH" "$STORM_BUNDLED" "$APP_BIN"
    STORM_BUILD_RPATH="$(cd "${BUILD_DIR}/_deps/stormlib-build" && pwd)"
    install_name_tool -delete_rpath "$STORM_BUILD_RPATH" "$APP_BIN" 2>/dev/null || true
fi

echo "=== Running macdeployqt ==="
if command -v macdeployqt &>/dev/null; then
    if ! macdeployqt "${BUILD_DIR}/ZipFX.app" -verbose=1; then
        echo "Warning: macdeployqt reported errors (app may still work)"
    fi
elif [ -x "${QT_DIR}/bin/macdeployqt" ]; then
    if ! "${QT_DIR}/bin/macdeployqt" "${BUILD_DIR}/ZipFX.app" -verbose=1; then
        echo "Warning: macdeployqt reported errors (app may still work)"
    fi
else
    echo "macdeployqt not found — skipping (app should still work for development)"
fi

echo "=== Copying lib7z.so ==="
LIB7Z=""
for path in ../lib/macos/arm64/lib7z.so ../lib/macos/lib7z.so; do
    if [ -f "$path" ]; then LIB7Z="$path"; break; fi
done
if [ -n "$LIB7Z" ]; then
    mkdir -p "${BUILD_DIR}/ZipFX.app/Contents/MacOS"
    cp -f "$LIB7Z" "${BUILD_DIR}/ZipFX.app/Contents/MacOS/"
    chmod 644 "${BUILD_DIR}/ZipFX.app/Contents/MacOS/lib7z.so"
fi

# Copy LICENSE into Resources
cp ../LICENSE "${BUILD_DIR}/ZipFX.app/Contents/Resources/LICENSE"

# Fix misplaced AppIcon.png (sometimes lands in MacOS/ instead of Resources/)
if [ -f "${BUILD_DIR}/ZipFX.app/Contents/MacOS/AppIcon.png" ]; then
    mv "${BUILD_DIR}/ZipFX.app/Contents/MacOS/AppIcon.png" \
       "${BUILD_DIR}/ZipFX.app/Contents/Resources/AppIcon.png"
fi

echo "=== Ad-hoc signing ==="
codesign --force --deep --sign - "${BUILD_DIR}/ZipFX.app"

echo "=== Creating DMG ==="
PKG_NAME="ZipFX-macOS.dmg"
# Use a temporary background image if one exists, otherwise create without one
DMG_BG=""
for path in ../resources/dmg-background.png ../resources/dmg-background.tiff; do
    if [ -f "$path" ]; then DMG_BG="$path"; break; fi
done
if command -v create-dmg &>/dev/null; then
    if [ -n "$DMG_BG" ]; then
        create-dmg \
            --volname "ZipFX" \
            --window-pos 200 120 \
            --window-size 600 450 \
            --icon-size 100 \
            --icon "ZipFX.app" 175 120 \
            --hide-extension "ZipFX.app" \
            --app-drop-link 425 120 \
            --background "$DMG_BG" \
            "${PKG_NAME}" \
            "${BUILD_DIR}/ZipFX.app"
    else
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
    fi
else
    hdiutil create -volname "ZipFX" -srcfolder "${BUILD_DIR}/ZipFX.app" \
        -ov -format UDZO "${PKG_NAME}"
fi

echo "=== Done: ${PKG_NAME} ==="
