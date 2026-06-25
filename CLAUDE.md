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
