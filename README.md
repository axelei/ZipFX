# ZipFX

**Multi-format archive manager for power users.**

ZipFX is a cross-platform GUI archive manager built with Qt6. It supports
a wide range of archive formats through multiple backends: **libzip** for ZIP,
**libarchive** for 7z, RAR, ISO, CAB, LHA, XAR, CPIO, **Bit7z** (7-Zip engine)
for extended format support, and native engines for game archive formats,
Amiga Disk Files, and more.

---

## Features

- **Create, extract, view, and test** archives across dozens of formats
- **Hierarchical and flat file tree browsing** with column sorting
- **Recursive folder add** from filesystem (drag & drop or dialog)
- **In-place ZIP modification** (add/delete files without full rewrite)
- **Windows shell drag & drop** with full directory structure preservation
- **CLI mode** (`list`, `extract`, `create`, `test`, `info` subcommands)
- **16 languages**: English, Spanish, French, German, Italian, Portuguese,
  Dutch, Swedish, Norwegian, Danish, Finnish, Russian, Japanese, Chinese,
  Korean, Arabic
- **File preview**: text / hex dump / image viewer with resize-to-fit
- **Archive information panel** (path, format, file/folder counts, packed
  and unpacked sizes, compression ratio)
- **Search/filter bar** (case-insensitive substring matching)
- **Overwrite confirmation** with apply-to-all
- **Magic-number file type detection** — opens files regardless of extension
- **Progress dialogs with byte-rate ETA and cancel** for all operations
- **Cancel actually aborts the save** (engines check a cancel flag at
  safe abort points, worker thread keeps the UI responsive while waiting)
- **Extraction and save after-action** (sleep / hibernate / shutdown)
- **File rename** (right-click context menu; in-place for ZIP)
- **POSIX permissions column** displayed as `rwxrwxrwx`
- **App icon** on all platforms (`.ico` Windows, `.icns` macOS, `.png` Linux)

---

## Supported Formats

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| ZIP | ✅ | ✅ | libzip | In-place modify |
| 7z | ✅ | ✅ | Bit7z / libarchive | Full write via Bit7z (AES-256, header encrypt, solid, multi-volume), read via libarchive fallback |
| RAR | ✅ | ❌ | libarchive | |
| RAR5 | ✅ | ❌ | libarchive | |
| ISO | ✅ | ❌ | libarchive | |
| CAB | ✅ | ❌ | libarchive | |
| LHA/LZH | ✅ | ❌ | libarchive | |
| XAR | ✅ | ❌ | libarchive | |
| CPIO | ✅ | ❌ | libarchive | |
| AR | ✅ | ❌ | libarchive | `.a`, `.deb` |
| TAR.GZ | ✅ | ✅ | zlib + manual tar | Full create + extract; rewrites on save |
| JAR, APK, DOCX, XLSX, PPTX, ODT, ODS, ODP, EPUB, WAR, EAR | ✅ | ✅ | libzip | ZIP-based formats |

### Game Archive Formats (native, no compression)

| Format | Read | Write | Backend | Used By |
|--------|------|-------|---------|---------|
| WAD | ✅ | ✅ | FlatArchiveEngine | Doom (IWAD/PWAD/WAD2/WAD3) |
| PAK | ✅ | ✅ | FlatArchiveEngine | Quake / Half-Life (PACK) |
| GRP | ✅ | ✅ | FlatArchiveEngine | Duke Nukem 3D (KenSilverman) |
| HOG | ✅ | ✅ | FlatArchiveEngine | Descent (HOG) |
| VPK | ✅ | ✅ | FlatArchiveEngine | Valve (Source engine games) |

### Disk / CD Image Formats

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| VHD | ✅ | ❌ | Bit7z | Magic `conectix` |
| VMDK | ✅ | ❌ | Bit7z | Magic `KDMV` |
| QCOW2 | ✅ | ❌ | Bit7z | Magic `QFI\xFB` |
| NRG | ✅ | ❌ | Bit7z | Nero CD images |
| BIN/CUE | ✅ | ❌ | Bit7z | Extension-based |

### Amiga Disk Files

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| ADF | ✅ | ❌ | AdfEngine (ADFlib) | Magic `DOS` at offset 0 |

### Additional Formats (via Bit7z / 7z.dll)

ARJ, CHM, DEB, DMG, EXT, FAT, GPT, HFS, HXS, IHEX, LZMA, MBR,
MSI, NSIS, NTFS, RPM, SquashFS, UDF, UEFI, VDI, WIM, Z — and
many more that 7-Zip supports.

### 7-Zip Engine (Bit7z)

The Bit7z backend handles formats not covered by libarchive (ARJ, DMG,
MSI, NRG, VHD, VMDK, etc.) and provides full **7z creation** with AES-256
encryption, header encryption, solid compression, dictionary tuning, and
multi-volume support.

It requires the 7-Zip shared library at runtime:

| Platform | Library | Source |
|----------|---------|--------|
| Windows | `7z.dll` | [7-Zip](https://www.7-zip.org/) installation or `lib/win/` |
| Linux | `lib7z.so` | Ubuntu `7zip` package or build from [p7zip source](https://github.com/p7zip-project/p7zip) |
| macOS | `lib7z.so` | Build from [p7zip source](https://github.com/p7zip-project/p7zip) (`CPP/7zip/Bundles/Format7zF/makefile.gcc`) |

---

## Building

### Requirements

- **CMake** ≥ 3.20
- **C++20** compiler (GCC, Clang, MSVC, MinGW)
- **Qt6** (Widgets module)

### Dependencies

The following are fetched automatically by CMake via `FetchContent`:

- **zlib** — compression
- **libzip** — ZIP read/write
- **libarchive** — 7z, RAR, ISO, CAB, LHA, XAR, CPIO, AR
- **bit7z** — extended format support via 7-Zip engine
- **ADFlib** — Amiga Disk File format (.adf)
- **CLI11** — command-line interface

### Build steps

```bash
git clone https://github.com/axelei/ZipFX.git
cd ZipFX
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
```

#### Windows (MinGW)

Use Qt's MinGW toolchain, not the one bundled with CLion (CLion's
bundled MinGW has incompatible `off_t`/`mode_t` definitions that
break libarchive and libzip).

```bash
set PATH=C:\Qt\6.x.x\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%

cmake -B build -G "MinGW Makefiles" ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake --build build
```

**CLion users:** Go to **Settings → Build, Execution, Deployment →
Toolchains** and change the toolchain to `C:\Qt\Tools\mingw1310_64`.
Then **File → Reload CMake Project**. If CMake compiler detection
still picks Gow's `make.exe`, set Environment in CMake settings to
`PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%`.

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
    ├── x64/lib7z.so
    └── arm64/lib7z.so
```

If the library is not found, Bit7z-based formats will be unavailable
but all other engines continue to work normally.

---

## Architecture

```
ArchiveEngine (pure virtual interface)
├── ZipEngine (libzip) — ZIP read/write, in-place modify
├── TarGzEngine (zlib + manual tar) — TAR.GZ read/write
├── LibarchiveEngine — parameterized with format registration
│   functions for 7z, RAR, RAR5, ISO, CAB, LHA, XAR, CPIO, AR
├── Bit7zEngine (7-Zip DLL/SO) — extended formats,
│   7z write with AES-256, fallback for exotic formats
├── AdfEngine (ADFlib) — Amiga Disk Files
├── FlatArchiveEngine (base class)
│   ├── WadEngine — Doom IWAD/PWAD/WAD2/WAD3
│   ├── PakEngine — Quake PACK
│   ├── GrpEngine — Duke Nukem 3D GRP
│   ├── HogEngine — Descent HOG
│   └── VpkEngine — Valve VPK
└── [Bit7z fallback] — last-resort auto-detect via 7z.dll
```

- **Format detection** uses magic bytes first (`FileSignature` table),
  then extension fallback. All format metadata is in a single
  `kFormats[]` table in `ArchiveEngineFactory.cpp`.
- **Write support on flat archives** uses a common `doSave()` virtual
  method. Each subclass writes its own binary layout (header + entries
  + data). Data is cached lazily — existing entries are read from the
  original file only when `Save()` is called.
- **Save runs on a worker thread** to keep the UI responsive. Progress
  is reported via `SaveProgressCb` callback. Cancel sets an atomic flag
  that engines check at safe abort points.
- **CLI mode** is detected by argv pattern matching before Qt starts.
  Supports `list`, `extract`, `create`, `test`, `info` subcommands.

---

## CLI Usage

```bash
zipfx list archive.zip
zipfx extract archive.7z -o /tmp/out
zipfx create output.zip file1.txt file2.txt
zipfx create output.7z --password secret --encrypt-headers file.dat
zipfx test archive.rar
zipfx info archive.iso
zipfx --cli list archive.cab
```

---

## Translations

ZipFX supports **16 languages**: English, Spanish, French, German,
Italian, Portuguese, Dutch, Swedish, Norwegian, Danish, Finnish,
Russian, Japanese, Chinese, Korean, Arabic.

The language menu shows flag emojis with native language names.
The preference is persisted via `QSettings`.

Translation files are in `translations/`. To add a new language:

```bash
lupdate src/ -ts translations/zipfx_<code>.ts
# Edit the .ts file
lrelease translations/zipfx_<code>.ts
```

The `.qm` files are generated at build time via CMake's `file(GLOB *.ts)`
+ `lrelease` pipeline, and loaded at startup from several paths relative
to the executable.

---

## Acknowledgements

- [Qt](https://www.qt.io/) — cross-platform UI framework
- [libzip](https://libzip.org/) — ZIP archive library
- [libarchive](https://www.libarchive.org/) — multi-format archive library
- [zlib](https://zlib.net/) — compression library
- [Bit7z](https://github.com/rikyoz/bit7z) — 7-Zip engine C++ wrapper
- [7-Zip](https://www.7-zip.org/) by Igor Pavlov — 7z compression engine
- [p7zip](https://github.com/p7zip-project/p7zip) — 7-Zip port for POSIX systems
- [ADFlib](https://github.com/adflib/ADFlib) — Amiga Disk File library
- [CLI11](https://github.com/CLIUtils/CLI11) — command-line parser

---

## License

GPLv3 — see [LICENSE](LICENSE).
