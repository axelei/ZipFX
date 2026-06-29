# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ZipFX is a cross-platform GUI archive manager built with Qt6 and C++20. It supports dozens of archive formats through multiple backends (libzip, libarchive, bit7z/7-Zip, zlib, bzip2, liblzma, libzstd, liblz4, ADFlib, StormLib) plus native parsers for game archive formats (WAD, PAK, GRP, HOG, VPK, GOB, RFF, BIG, POD). It includes both a Qt GUI and a CLI mode.

## Build Commands

```bash
# Configure (Windows MinGW — must use Qt's MinGW, not CLion's bundled one)
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

# Configure (macOS)
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)

# Configure (Linux)
cmake -B build

# Build (full)
cmake --build build

# Build only the main executable (faster; skips test relink)
cmake --build build --target ZipFX

# Compile a single file without linking (fastest syntax check)
cmake --build build -- CMakeFiles/ZipFX.dir/src/engine/TarGzEngine.cpp.obj
# Object files live at: build/CMakeFiles/ZipFX.dir/<src-relative-path>.cpp.obj

# Run tests (all)
cd build && ctest --output-on-failure

# Run a single test
cd build && ctest -R tst_ZipEngine --output-on-failure
# Or run the test executable directly:
build/tests/tst_ZipEngine
```

All dependencies (zlib, bzip2, liblzma, libzstd, liblz4, libzip, libarchive, bit7z, ADFlib, StormLib, libchdr, CLI11) are fetched automatically via CMake FetchContent. Only Qt6 must be pre-installed.

Tests use the Qt Test framework (`QTest`). Each test file is a standalone executable registered via `add_zipfx_test()` in `tests/CMakeLists.txt`. On Windows, test executables need Qt and libzip DLLs on PATH (the CMake config handles this for `ctest`).

**Test files:** `tst_ZipEngine`, `tst_TarGzEngine`, `tst_LibarchiveEngine`, `tst_MpqEngine`, `tst_FlatArchiveEngines`, `tst_ArchiveEngineFactory`, `tst_FileSignature`, `tst_FileListModel`, `tst_CliHandler`.

## Architecture

### Engine Hierarchy

All archive format support flows through `ArchiveEngine` (pure virtual base in `src/engine/ArchiveEngine.h`). The factory (`ArchiveEngineFactory`) selects the right engine using magic-byte detection first (`FileSignature::Detect` returns an `ArchiveType` enum value), then extension fallback. The format registry is a single `kFormats[]` table in `ArchiveEngineFactory.cpp`.

Key engine subclasses:
- **ZipEngine** — libzip; in-place modification (add/delete without rewrite)
- **TarGzEngine** — zlib + manual tar header parsing/writing; `ReadFile`/`ReadFilePartial` and `Extract` all reopen the gzip stream from position 0 on each call (gzip is sequential, no random access)
- **LibarchiveEngine** — parameterized with format/filter registrar function pointers; used for 7z, RAR, ISO, CAB, LHA, XAR, CPIO, AR, WARC, MTREE (read-only), and compressed tar variants (tar.bz2, tar.xz, tar.zst, tar.lz4, tar.lzma) plus standalone compression (bz2, xz, zst, lz4, lzma)
- **Bit7zEngine** — wraps 7-Zip DLL via bit7z; provides write support for 7z and read for exotic formats; loaded dynamically (gracefully absent). RAR uses `setReadOnly(true)` so the UI disables writing even though the library is loaded. `m_reader` must be `reset()` before `Save()` to release the Windows file handle.
- **FlatArchiveEngine** — base class for headerless game archives; subclasses override `Open()` and `doSave()` to handle format-specific binary layouts (WadEngine, PakEngine, GrpEngine, HogEngine, VpkEngine, GobEngine, RffEngine, BigEngine, PodEngine)
- **ChdEngine** — libchdr wrapper for MAME Compressed Hunks of Data disc images (.chd); presents CD-ROM tracks or raw disc image as extractable entries
- **AdfEngine** — ADFlib wrapper for Amiga floppy disk images
- **MpqEngine** — StormLib wrapper for Blizzard MPQ archives

### ArchiveEntry fields

`ArchiveEntry` (defined in `src/engine/ArchiveEntry.h`) has both `name` and `path`. In practice both hold the same archive-relative forward-slash path (e.g. `"folder/file.txt"`). Code that looks up entries should check both (`e.name == key || e.path == key`) since engines populate them slightly differently. The `FileListModel` stores `fullPath = e.path` and returns it from `selectedEntryPaths()`; the engine's `ReadFile`/`Extract` methods receive this value as `entryName`.

### Engine capability flags

`ArchiveEngine` exposes several capability predicates used by the UI to enable/disable actions:
- `SupportsCreation()` — engine can add files; `dragEnterEvent` and the Add toolbar button check this
- `SupportsViewFile()` — engine supports in-place file reading (returns false for solid/multi-volume bit7z archives)
- `ViewUnsupportedReason()` — human-readable reason when `SupportsViewFile()` is false

When adding a new read-only format that uses `Bit7zEngine`, call `setReadOnly(true)` on the instance in the factory lambda so `SupportsCreation()` returns false.

### Write Flow

Engines that support writing queue additions via `AddFile()` and commit on `Save()`. There are two save paths in `MainWindow`:

- **`saveWithProgress()`** — used when a `m_progressDlg` is already on screen (add-file flow). Runs `Save()` on a `std::thread`, polls the progress callback for byte-rate ETA, supports cancellation via `cancelSave()`.
- **`runSave(label)`** — lightweight modal dialog with indeterminate progress bar; used for delete, rename, and comment updates where no byte-level progress is available.

Progress is reported via `SaveProgressCb`; cancellation uses `std::atomic<bool> m_saveCancelled`.

### UI Layer

- **MainWindow** (`src/ui/MainWindow.h`) — main window, menus, toolbar, drag-and-drop, extraction/save orchestration
- **FileListModel** (`src/ui/FileListModel.h`) — `QAbstractItemModel` that presents archive entries in hierarchical or flat view with filtering, sorting, and directory navigation. Columns are defined by the `Column` enum: `ColName`, `ColSize`, `ColPacked`, `ColType`, `ColModified`, `ColCRC`, `ColPermissions`, `ColComment`.
- **CreateArchiveDialog** — new archive wizard with format/compression/encryption options
- **ArchiveTreeView** — custom `QTreeView` that intercepts drag to use platform-native virtual file drag (Windows COM `IDataObject`, macOS file promises, Linux temp-file `QDrag`)

### CLI

CLI mode is detected by argv pattern in `main.cpp` before Qt starts. Dispatches to `runCli()` in `src/cli/CliHandler.cpp` which uses CLI11 for parsing. Subcommands: `list`, `extract`, `create`, `test`, `info`.

## Key Patterns

- Engines store a cached `std::vector<ArchiveEntry> m_entries` populated on `Open()`. `ListContents()` returns a const reference.
- `FlatArchiveEngine::Save()` reads all entry data into memory via `ReadFile()` before writing, so the file can be overwritten in place.
- Platform-specific code is guarded by `#ifdef _WIN32`, `#ifdef __APPLE__`, `Q_OS_MACOS`. Windows drag-and-drop uses COM `IDataObject` in `src/dnd/VirtualFileDataObject.cpp`.
- Logging uses printf-style macros (`LOG_DBG`, `LOG_WARN`, `LOG_ERR`) defined in `src/engine/Logging.h`, wrapping Qt's `qDebug`/`qWarning`/`qCritical`.
- The `ProgressInfo` struct (`src/ui/ProgressInfo.h`) provides EWMA-smoothed byte-rate ETA for progress dialogs.
- `ReadFile` and `ReadFilePartial` in TarGzEngine use 64 KB chunked `gzread` loops (mirroring `Extract`) — a single large `gzread` call can return fewer bytes than requested due to zlib's internal buffer size.

## Platform Notes

- **Windows**: requires Qt's MinGW toolchain specifically (CLion's bundled MinGW has incompatible `off_t`/`mode_t`). The `--allow-multiple-definition` linker flag is needed. `7z.dll` must be next to the executable or in `lib/win/x64/`.
- **macOS**: drag-and-drop uses file promises via `src/dnd/MacPromiseDrag.mm` (Obj-C++).
- File associations are registered via Windows registry on first launch (`registerFileAssociations` in MainWindow.cpp).

## Session Context: FUSE Drag-and-Drop & AppImage

### Goal
- Fix FUSE drag-and-drop on Linux: eliminate the fd-close-vs-read crash, make file managers accept the drag under Wayland, and produce a portable AppImage.

### Constraints
- Ubuntu 26.04, native Wayland. Qt 6.11.1 (manual install at `/home/krusher/Qt/6.11.1/gcc_64`). libfuse3 3.18.2 (SONAME `libfuse3.so.4` on this system).
- DND is broken under XWayland -- Qt must use the `wayland` QPA plugin.
- AppImage must run on older distros (no GLIBC_2.43 dependency from bundled system libraries).

### Progress

**Done:**
- **FUSE crash fixed** (`src/dnd/FuseArchiveMount.cpp`): replaced `fuse_loop(f)` with custom poll-based loop (1s timeout) using `fuse_session_fd` + `fuse_session_receive_buf` + `fuse_session_process_buf`. The FUSE thread calls `fuse_unmount()`/`fuse_destroy()` on itself -- no cross-thread fd close race.
- **Reverted `ZIPFX_USE_FUSE` opt-in**: FUSE remains the default (controlled by `ZIPFX_NO_FUSE`).
- **Wayland QPA plugin bundled** in AppImage: copies `libqwayland.so`, `libQt6WaylandClient.so*`, `libQt6OpenGL.so*`, plus `wayland-shell-integration/`, `wayland-graphics-integration-client/`, `wayland-decoration-client/` plugin dirs.
- **Wayland auto-detection** (`src/main.cpp`): if `WAYLAND_DISPLAY` is set and `QT_QPA_PLATFORM` not overridden, sets `QT_QPA_PLATFORM=wayland` before `QApplication` construction.
- **`QT_WAYLAND_APP_ID=zipfx`** set via `qputenv` in `main.cpp` (inside the Wayland block) so the compositor sees the same app_id as the desktop file.
- **Drag icon fixed**: dark background (`#2d2d2d`) + app icon + white text on both FUSE and eager-extraction paths (was transparent + white text, invisible on light compositors).
- **libglib excluded from AppImage**: `libglib-2.0.so.0` (bundled from Ubuntu 26.04) references `GLIBC_2.43`, breaking on older distros. Build script runs `linuxdeploy --plugin qt` (fills AppDir), removes glib, then packages with `appimagetool`.
- **AppIcon.png installed next to executable** (`deploy/build_linux.sh`): added `install(FILES ... DESTINATION "bin")` in `CMakeLists.txt` and fixed cleanup step to preserve it during packaging.
- **libfuse3 SONAME `libfuse3.so.4`** on Ubuntu 26.04 build system; build script updated to detect actual filename.
- **`registerFileAssociationsLinux()`** in `MainWindow.cpp` — runs on every Linux launch (not cached). Writes `~/.local/share/applications/zipfx.desktop` with `StartupWMClass=zipfx` and `Icon=zipfx`; creates symlink `ZipFX.desktop` → `zipfx.desktop`; copies `AppIcon.png` to `~/.local/share/icons/hicolor/{64,256}x256/apps/zipfx.png`; runs `gtk-update-icon-cache --ignore-theme-index` and `update-desktop-database`.
- **GNOME dock icon fixed** — root cause was the user-local `~/.local/share/icons/hicolor/index.theme` created by an earlier version of the registration code. It had `Hidden=true` and no `Directories=` line, which caused GTK/GNOME Shell to skip hicolor entirely for icon lookups, falling back to the generic gear. Fix: never create a user-local `index.theme` (the system hicolor theme is authoritative). `gtk-update-icon-cache` uses `--ignore-theme-index` to avoid needing one.

### Key Decisions
- **Poll loop over signals/self-pipe**: simpler, no signal handlers, no extra fds. 1s timeout is acceptable (extraction already in memory).
- **Wayland auto-detection over wrapper script**: `main.cpp` sets `QT_QPA_PLATFORM` via `qputenv` when `WAYLAND_DISPLAY` is present -- respects explicit override.
- **Split linuxdeploy into two steps**: first `linuxdeploy --appdir --plugin qt`, then manual `appimagetool` after removing `libglib`. Can't use `--output appimage` alone because it re-deploys excluded libraries.
- **Icon in `usr/bin/`**: `MainWindow.cpp` loads window icon from `applicationDirPath() + "/AppIcon.png"`. Must be next to executable.
- **`QT_WAYLAND_APP_ID` over `setDesktopFileName()`**: `setDesktopFileName()` triggered a portal error (`"Connection already associated with an application ID"`) because the portal binds an app_id before Qt can set one. `QT_WAYLAND_APP_ID` env var avoids the portal entirely, letting Qt's Wayland plugin set the app_id at the xdg-shell protocol level.
- **Desktop file always rewritten** every launch (not cached via QSettings) so field additions like `StartupWMClass` and `Icon=zipfx` reach existing installs.
- **Never create user-local `index.theme`**: A minimal user-local `index.theme` with `Hidden=true` and missing `Directories=` silently disables the hicolor theme, making all custom icons unresolvable. The system `/usr/share/icons/hicolor/index.theme` is authoritative. Use `gtk-update-icon-cache --ignore-theme-index` to build the cache without a local `index.theme`.

### Critical Context
- `QDrag::exec` returns `0` (IgnoreAction) under XWayland. Native Wayland (`QT_QPA_PLATFORM=wayland`) fixes DND entirely.
- `fuse_session_process_buf(se, &fbuf)` in libfuse3 3.18.2 takes only 2 arguments (`ch` parameter was dropped).
- The only library referencing `GLIBC_2.43` in the original AppImage was `libglib-2.0.so.0`. Excluding it is safe (glib is on every Linux system).
- Build system has both `libfuse3.so.3.18.2` and `libfuse3.so.4` in `/usr/lib/x86_64-linux-gnu/`, all with SONAME `libfuse3.so.4`.
- cmake config from `deploy/` dir: `cmake -S .. -B build_linux`. Build with `--target ZipFX`. Install with `cmake --install build_linux --prefix build_linux/install/usr`.
- **GNOME icon resolution on Wayland**: The compositor determines the dock/bar icon via `app_id` → `{app_id}.desktop` → `Icon=` → icon theme lookup. `setWindowIcon()` has NO effect on GNOME Wayland (no `xdg-toplevel-icon-v1` protocol support in Mutter). The icon must be registered through the GTK icon theme.
- **Portal error**: `qt.qpa.services: Failed to register with host portal QDBusError(..., "Connection already associated with an application ID")`. This occurs when calling `setDesktopFileName()` because the portal derives the app_id from the D-Bus connection independently (using the process basename). Using `QT_WAYLAND_APP_ID` env var avoids this entirely.
- **`APPIMAGE` env var** is NOT set in the actual ZipFX process inside the AppImage (it's only set in the intermediate AppRun process). `registerFileAssociationsLinux()` falls back to `applicationFilePath()`, which correctly returns the AppImage path via `/proc/self/exe` symlink.
- **Process name is `ZipFX-Linux.App`** (truncated to 15 chars by Linux `comm` limit) when running inside the AppImage. The `QT_WAYLAND_APP_ID=zipfx` and the `ZipFX.desktop → zipfx.desktop` symlink handle any app_id derivation mismatch.

### Relevant Files
- `src/dnd/FuseArchiveMount.cpp` -- poll-based loop replacing `fuse_loop(f)`; `unmount()` sets `m_unmountRequested` instead of calling `fuse_unmount`; includes `<fuse_lowlevel.h>` and `<poll.h>`.
- `src/dnd/FuseArchiveMount.h` -- `std::atomic<bool> m_unmountRequested{false}`.
- `src/ui/MainWindow.cpp` -- `ZIPFX_NO_FUSE` env var check; drag pixmap with dark background + app icon; `registerFileAssociationsLinux()` writes desktop file + icons on every Linux launch; removed `index.theme` creation.
- `src/ui/MainWindow.h` -- declared `registerFileAssociationsLinux()`.
- `src/main.cpp` -- Wayland auto-detection (`qputenv("QT_QPA_PLATFORM", "wayland")`) and `qputenv("QT_WAYLAND_APP_ID", "zipfx")` before `QApplication` creation.
- `deploy/build_linux.sh` -- Wayland plugin bundling; glib removal; AppIcon.png copy and preservation in cleanup.
- `CMakeLists.txt` -- `install(FILES AppIcon.png DESTINATION "bin")` for Linux.
