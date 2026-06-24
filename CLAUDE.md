# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ZipFX is a cross-platform GUI archive manager built with Qt6 and C++20. It supports dozens of archive formats through multiple backends (libzip, libarchive, bit7z/7-Zip, zlib, ADFlib, StormLib) plus native parsers for game archive formats (WAD, PAK, GRP, HOG, VPK). It includes both a Qt GUI and a CLI mode.

## Build Commands

```bash
# Configure (Windows MinGW — must use Qt's MinGW, not CLion's bundled one)
cmake -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64

# Configure (macOS)
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)

# Configure (Linux)
cmake -B build

# Build
cmake --build build

# Run tests (all)
cd build && ctest --output-on-failure

# Run a single test
cd build && ctest -R tst_ZipEngine --output-on-failure
# Or run the test executable directly:
build/tests/tst_ZipEngine
```

All dependencies (zlib, libzip, libarchive, bit7z, ADFlib, StormLib, CLI11) are fetched automatically via CMake FetchContent. Only Qt6 must be pre-installed.

Tests use the Qt Test framework (`QTest`). Each test file is a standalone executable registered via `add_zipfx_test()` in `tests/CMakeLists.txt`. On Windows, test executables need Qt and libzip DLLs on PATH (the CMake config handles this for `ctest`).

## Architecture

### Engine Hierarchy

All archive format support flows through `ArchiveEngine` (pure virtual base in `src/engine/ArchiveEngine.h`). The factory (`ArchiveEngineFactory`) selects the right engine using magic-byte detection first (`FileSignature::Detect`), then extension fallback. The format registry is a single `kFormats[]` table in `ArchiveEngineFactory.cpp`.

Key engine subclasses:
- **ZipEngine** — libzip; in-place modification (add/delete without rewrite)
- **TarGzEngine** — zlib + manual tar header parsing/writing
- **LibarchiveEngine** — parameterized with format registrar function pointers; used for 7z, RAR, ISO, CAB, LHA, XAR, CPIO, AR (read-only)
- **Bit7zEngine** — wraps 7-Zip DLL via bit7z; provides write support for 7z and read for exotic formats; loaded dynamically (gracefully absent)
- **FlatArchiveEngine** — base class for headerless game archives; subclasses override `Open()` and `doSave()` to handle format-specific binary layouts (WadEngine, PakEngine, GrpEngine, HogEngine, VpkEngine)
- **AdfEngine** — ADFlib wrapper for Amiga floppy disk images
- **MpqEngine** — StormLib wrapper for Blizzard MPQ archives

### Write Flow

Engines that support writing queue additions via `AddFile()` and commit on `Save()`. Save runs on a worker thread (`std::thread` in `MainWindow::saveWithProgress`); progress is reported via `SaveProgressCb`; cancellation uses `std::atomic<bool> m_saveCancelled`.

### UI Layer

- **MainWindow** (`src/ui/MainWindow.h`) — main window, menus, toolbar, drag-and-drop, extraction/save orchestration
- **FileListModel** (`src/ui/FileListModel.h`) — `QAbstractItemModel` that presents archive entries in hierarchical or flat view with filtering, sorting, and directory navigation
- **CreateArchiveDialog** — new archive wizard with format/compression/encryption options
- **ArchiveTreeView** — custom `QTreeView` that intercepts drag to use platform-native virtual file drag (Windows COM `IDataObject`, macOS file promises, Linux temp-file `QDrag`)

### CLI

CLI mode is detected by argv pattern in `main.cpp` before Qt starts. Dispatches to `runCli()` in `src/cli/CliHandler.cpp` which uses CLI11 for parsing. Subcommands: `list`, `extract`, `create`, `test`, `info`.

## Key Patterns

- Engines store a cached `std::vector<ArchiveEntry> m_entries` populated on `Open()`. `ListContents()` returns a copy.
- `FlatArchiveEngine::Save()` reads all entry data into memory via `ReadFile()` before writing, so the file can be overwritten in place.
- Platform-specific code is guarded by `#ifdef _WIN32`, `#ifdef __APPLE__`, `Q_OS_MACOS`. Windows drag-and-drop uses COM `IDataObject` in `src/dnd/VirtualFileDataObject.cpp`.
- Logging uses printf-style macros (`LOG_DBG`, `LOG_WARN`, `LOG_ERR`) defined in `src/engine/Logging.h`, wrapping Qt's `qDebug`/`qWarning`/`qCritical`.
- The `ProgressInfo` struct (`src/ui/ProgressInfo.h`) provides EWMA-smoothed byte-rate ETA for progress dialogs.

## Platform Notes

- **Windows**: requires Qt's MinGW toolchain specifically (CLion's bundled MinGW has incompatible `off_t`/`mode_t`). The `--allow-multiple-definition` linker flag is needed. `7z.dll` must be next to the executable or in `lib/win/x64/`.
- **macOS**: drag-and-drop uses file promises via `src/dnd/MacPromiseDrag.mm` (Obj-C++).
- File associations are registered via Windows registry on first launch (`registerFileAssociations` in MainWindow.cpp).
