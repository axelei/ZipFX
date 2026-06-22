# ZipFX

**Multiplatform archiver for power users.**

ZipFX is a cross-platform GUI archive manager built with Qt6. It supports
a wide range of archive formats through multiple backends: **libzip** for ZIP,
**libarchive** for 7z, RAR, ISO, CAB, LHA, XAR, CPIO, and **Bit7z** (7-Zip engine)
for additional formats like ARJ, DMG, MSI, NSIS, VDI, VHD, VMDK, WIM, and more.

---

## Features

- Create, extract, view, and test archives
- Hierarchical and flat file tree browsing
- Recursive folder add from filesystem (drag & drop or dialog)
- In-place ZIP modification (add/delete files without full rewrite)
- Windows shell drag & drop with full directory structure preservation
- Multi-engine architecture: picks the best backend for each format
- 7-Zip engine for extended format coverage (when `7z.dll` / `lib7z.so` is available)
- Progress dialogs with cancellation for all operations
- File preview: text / hex dump / image viewer
- Archive information panel (size, ratio, file/folder counts)
- Search/filter bar
- Overwrite confirmation with apply-to-all
- Integrity testing with progress
- Extraction with after-action (sleep/hibernate/shutdown)
- Column sorting (clickable headers)
- Translation support (English, Spanish)
- Magic-number file type detection (opens files regardless of extension)

---

## Supported Formats

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| ZIP | ✅ | ✅ | libzip | In-place modify |
| 7z | ✅ | ⚠️ | libarchive | Write support stub |
| RAR | ✅ | ❌ | libarchive | |
| RAR5 | ✅ | ❌ | libarchive | |
| ISO | ✅ | ❌ | libarchive | |
| CAB | ✅ | ❌ | libarchive | |
| LHA/LZH | ✅ | ❌ | libarchive | |
| XAR | ✅ | ❌ | libarchive | |
| CPIO | ✅ | ❌ | libarchive | |
| TAR.GZ | ✅ | ✅ | zlib + manual tar | Rewrites on save |
| JAR, APK, DOCX, XLSX, PPTX, ODT, ODS, ODP, EPUB, WAR, EAR | ✅ | ✅ | libzip | ZIP-based formats |
| ARJ, CHM, DEB, DMG, EXT, FAT, GPT, HFS, HXS, IHEX, LZMA, MBR, MSI, NSIS, NTFS, QCOW2, RPM, SquashFS, UDF, UEFI, VDI, VHD, VMDK, WIM, Z | ✅ | ❌ | Bit7z | Requires 7z.dll/so |

### 7-Zip Engine (Bit7z)

The Bit7z backend handles formats not covered by libarchive. It requires the
7-Zip shared library at runtime:

| Platform | Library | Source |
|----------|---------|--------|
| Windows | `7z.dll` | [7-Zip](https://www.7-zip.org/) installation or `lib/win/` |
| Linux | `lib7z.so` | Ubuntu `7zip` package or build from [source](https://github.com/p7zip-project/p7zip) |
| macOS | `lib7z.so` | Build from [p7zip source](https://github.com/p7zip-project/p7zip) (`CPP/7zip/Bundles/Format7zF/makefile.gcc`) |

---

## Building

### Requirements

- **CMake** ≥ 3.20
- **C++17** compiler (GCC, Clang, MSVC, MinGW)
- **Qt6** (Widgets module)

### Dependencies

The following are fetched automatically by CMake via `FetchContent`:
- **zlib** — compression
- **libzip** — ZIP read/write
- **libarchive** — 7z, RAR, ISO, CAB, LHA, XAR, CPIO read
- **bit7z** — extended format support via 7-Zip engine

### Build steps

```bash
git clone https://github.com/axelei/ZipFX.git
cd ZipFX
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
```

#### Windows (MinGW)

```bash
cmake -B build -G "MinGW Makefiles" \
    -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake --build build
```

#### macOS

```bash
brew install qt
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

#### Linux

```bash
sudo apt install qt6-base-dev
cmake -B build
cmake --build build
```

### macOS Gatekeeper

If macOS blocks the app because it is not notarized:

```bash
xattr -rd com.apple.quarantine /path/to/ZipFX.app
```

Or codesign it yourself:

```bash
codesign --force --deep --sign - /path/to/ZipFX.app
```

---

## Library Placement

Pre-built 7-Zip shared libraries can be placed in `lib/` for automatic
bundling. The expected layout is:

```
lib/
├── win/
│   ├── x64/7z.dll
│   └── arm64/7z.dll
├── linux/
│   ├── x64/lib7z.so
│   └── arm64/lib7z.so
└── macos/
    ├── x64/lib7z.so        (build from p7zip source)
    └── arm64/lib7z.so      (build from p7zip source)
```

If the library is not found, Bit7z-based formats will be unavailable
but all other engines continue to work normally.

---

## Architecture

```
ArchiveEngine (pure virtual interface)
├── default: Extract via ReadFile + disk write
├── default: AddFile/RemoveEntry/Save → false
├── LibarchiveEngine (concrete)
│   └── Parameterized with format registration functions
├── ZipEngine (libzip)
├── TarGzEngine (zlib + manual tar)
└── Bit7zEngine (7-Zip DLL/SO, fallback)
```

Format detection uses magic bytes first, then extension fallback.
All format metadata is in a single `kFormats[]` table in
`ArchiveEngineFactory.cpp`.

---

## Translations

ZipFX uses Qt's `lupdate`/`lrelease` pipeline. Translation files are
in `translations/`. To add a new language:

```bash
lupdate src/ -ts translations/zipfx_<code>.ts
# Edit the .ts file
lrelease translations/zipfx_<code>.ts
```

The `.qm` files are generated at build time and loaded at startup
from several paths relative to the executable.

---

## Acknowledgements

- [Qt](https://www.qt.io/) — cross-platform UI framework
- [libzip](https://libzip.org/) — ZIP archive library
- [libarchive](https://www.libarchive.org/) — multi-format archive library
- [zlib](https://zlib.net/) — compression library
- [Bit7z](https://github.com/rikyoz/bit7z) — 7-Zip engine C++ wrapper
- [7-Zip](https://www.7-zip.org/) by Igor Pavlov — 7z compression engine
- [p7zip](https://github.com/p7zip-project/p7zip) — 7-Zip port for POSIX systems

---

## License

GPLv3 — see [LICENSE](LICENSE).
