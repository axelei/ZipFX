# ZipFX

**Multi-format archive manager for power users.**

ZipFX is a cross-platform GUI archive manager built with Qt6. It supports
a wide range of archive formats through multiple backends: **libzip** for ZIP,
**libarchive** for 7z, RAR, ISO, CAB, LHA, XAR, CPIO, compressed tars, and
standalone compression, **Bit7z** (7-Zip engine) for extended format support,
**libchdr** for MAME CHD disc images, and native engines for game archive
formats, Amiga Disk Files, and more.

---

## Features

### Browsing & Navigation
- **Hierarchical and flat file tree browsing** with column sorting
- **Search/filter bar** (case-insensitive substring matching)
- **Find Files dialog** (`Ctrl+F`) — search by name glob, size range, and date range; double-click navigates to the entry
- **Column visibility toggle** — right-click the column header to show/hide any column; choices are persistent across refreshes
- **Status bar** shows selection count and total size of selected files
- **Recent files list** — address bar remembers last 10 opened archives
- **Multi-volume VPK support** — reads from `_000.vpk`, `_001.vpk` volume files

### Extraction & Creation
- **Create, extract, view, and test** archives across dozens of formats
- **In-place ZIP modification** (add/delete files without full rewrite)
- **Recursive folder add** from filesystem (drag & drop or dialog)
- **Extract Here** — extract to the same directory as the archive
- **Extract without Paths** — strips directory structure on extraction
- **Exclude patterns** — glob-based patterns to skip files when adding
- **Keep Broken Files on Extraction** — configurable option to retain partial files
- **Windows shell drag & drop** with full directory structure preservation
- **Overwrite confirmation** with apply-to-all
- **Archive conversion** — convert any supported format to another (ZIP, 7z, TAR.GZ, TAR.BZ2, TAR.XZ) via extract-and-repack
- **Archive repair** — test integrity, extract what can be recovered, save as a new archive

### Batch & Automation
- **Batch operations** — test or extract all archives in a folder (recursive optional); live log per archive
- **CLI mode** (`list`, `extract`, `create`, `test`, `info` subcommands)
- **Extraction and save after-action** (sleep / hibernate / shutdown)

### Viewing & Preview
- **Preview pane** (toggle via Options menu) — shows text, hex dump, or image inline for the selected entry; reads only a 64 KB chunk for speed
- **File preview in viewer** — open or open-with for entries directly from the archive
- **Archive information panel** (path, format, file/folder counts, packed and unpacked sizes, compression ratio, compression method)
- **POSIX permissions column** displayed as `rwxrwxrwx`
- **Magic-number file type detection** — opens files regardless of extension

### Security & Passwords
- **Password manager** — save passwords per archive filename; auto-applied when opening; editable via Commands menu
- **Set Password** — prompts to save the password for next time
- **AES-256 encryption** for 7z archives (with optional header encryption)

### UI & UX
- **Select All / Invert Selection** (Edit menu, `Ctrl+A`)
- **Right-click context menu** with Open, Open With, Copy Path, Rename, Delete, Properties
- **File rename** (right-click context menu; in-place for ZIP)
- **16 languages**: English, Spanish, French, German, Italian, Portuguese,
  Dutch, Swedish, Norwegian, Danish, Finnish, Russian, Japanese, Chinese,
  Korean, Arabic
- **Progress dialogs with byte-rate ETA and cancel** for all operations
- **Cancel actually aborts the save** (engines check a cancel flag at safe abort points; worker thread keeps the UI responsive)
- **ADF creation** — create Amiga floppy images directly
- **App icon** on all platforms (`.ico` Windows, `.icns` macOS, `.png` Linux)

---

## Supported Formats

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| ZIP | ✅ | ✅ | libzip | In-place modify |
| 7z | ✅ | ✅ | Bit7z / libarchive | Full write via Bit7z (AES-256, header encrypt, solid, multi-volume), read via libarchive fallback |
| RAR | ✅ | ❌ | Bit7z / libarchive | Multi-volume support via 7z.dll |
| RAR5 | ✅ | ❌ | Bit7z / libarchive | |
| ISO | ✅ | ❌ | libarchive | |
| CAB | ✅ | ❌ | libarchive | |
| LHA/LZH | ✅ | ❌ | libarchive | |
| XAR | ✅ | ❌ | libarchive | |
| CPIO | ✅ | ❌ | libarchive | |
| AR | ✅ | ❌ | libarchive | `.a`, `.deb` |
| WARC | ✅ | ❌ | libarchive | Web archive format |
| MTREE | ✅ | ❌ | libarchive | BSD file hierarchy spec |
| TAR.GZ | ✅ | ✅ | zlib + manual tar | Full create + extract; rewrites on save |
| TAR.BZ2 | ✅ | ❌ | libarchive | `.tar.bz2`, `.tbz2` |
| TAR.XZ | ✅ | ❌ | libarchive | `.tar.xz`, `.txz` |
| TAR.ZST | ✅ | ❌ | libarchive | `.tar.zst`, `.tzst` |
| TAR.LZ4 | ✅ | ❌ | libarchive | `.tar.lz4` |
| TAR.LZMA | ✅ | ❌ | libarchive | `.tar.lzma` |
| BZ2 | ✅ | ❌ | libarchive | Standalone bzip2 |
| XZ | ✅ | ❌ | libarchive | Standalone xz |
| ZST | ✅ | ❌ | libarchive | Standalone Zstandard |
| LZ4 | ✅ | ❌ | libarchive | Standalone LZ4 |
| LZMA | ✅ | ❌ | libarchive | Standalone LZMA |
| JAR, APK, DOCX, XLSX, PPTX, ODT, ODS, ODP, EPUB, WAR, EAR | ✅ | ✅ | libzip | ZIP-based formats |

### Game Archive Formats

| Format | Read | Write | Backend | Used By |
|--------|------|-------|---------|---------|
| WAD | ✅ | ✅ | FlatArchiveEngine | Doom (IWAD/PWAD/WAD2/WAD3) |
| PAK | ✅ | ✅ | FlatArchiveEngine | Quake / Half-Life (PACK) |
| GRP | ✅ | ✅ | FlatArchiveEngine | Duke Nukem 3D (KenSilverman) |
| HOG | ✅ | ✅ | FlatArchiveEngine | Descent (HOG) |
| VPK | ✅ | ✅ | FlatArchiveEngine | Valve (Source engine games) |
| GOB | ✅ | ✅ | FlatArchiveEngine | Dark Forces / Jedi Knight |
| RFF | ✅ | ✅ | FlatArchiveEngine | Blood (Monolith) |
| BIG | ✅ | ✅ | FlatArchiveEngine | EA games (C&C, FIFA) — `.big`, `.viv` |
| POD | ✅ | ✅ | FlatArchiveEngine | Terminal Velocity / Fury3 |
| **MPQ** | ✅ | ✅ | StormLib | Warcraft III, StarCraft II, Diablo III, WoW |

### Disk / CD Image Formats

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| VHD | ✅ | ❌ | Bit7z | Magic `conectix` |
| VMDK | ✅ | ❌ | Bit7z | Magic `KDMV` |
| QCOW2 | ✅ | ❌ | Bit7z | Magic `QFI\xFB` |
| NRG | ✅ | ❌ | Bit7z | Nero CD images |
| BIN/CUE | ✅ | ❌ | Bit7z | Extension-based |
| IMA/IMG/FLP/DSK | ✅ | ❌ | Bit7z | Floppy/disk images |
| **CHD** | ✅ | ❌ | libchdr | MAME Compressed Hunks of Data; CD-ROM tracks, HDD/DVD images |

### Amiga Disk Files

| Format | Read | Write | Backend | Notes |
|--------|------|-------|---------|-------|
| ADF | ✅ | ✅ | AdfEngine (ADFlib) | Create floppy images (880 KB, FFS) |

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

- **zlib** — gzip compression
- **bzip2** — bzip2 compression
- **liblzma** (xz-utils) — xz/lzma compression
- **libzstd** — Zstandard compression
- **liblz4** — LZ4 compression
- **libzip** — ZIP read/write
- **libarchive** — 7z, RAR, ISO, CAB, LHA, XAR, CPIO, AR, WARC, compressed tars, standalone compression
- **bit7z** — extended format support via 7-Zip engine
- **ADFlib** — Amiga Disk File format (.adf)
- **StormLib** — Blizzard MPQ archive format
- **libchdr** — MAME Compressed Hunks of Data (.chd) disc images
- **CLI11** — command-line interface

### Build steps

```bash
git clone https://github.com/axelei/ZipFX.git
cd ZipFX
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
```

#### Windows (MinGW)

**Prerequisites:**

1. Install [Qt6](https://www.qt.io/download-qt-installer) — select the **MinGW 64-bit** component and the matching **MinGW toolchain** under *Qt → Tools*.
2. Install [CMake](https://cmake.org/download/) and ensure it is on your `PATH`.
3. *(Optional)* Install [7-Zip](https://www.7-zip.org/) if you want Bit7z support at runtime.

Use Qt's MinGW toolchain, not the one bundled with CLion (CLion's
bundled MinGW has incompatible `off_t`/`mode_t` definitions that
break libarchive and libzip).

```bat
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

**Prerequisites:**

1. Install [Homebrew](https://brew.sh/) if not already present.
2. Install Qt6 and the Xcode Command Line Tools:

```bash
xcode-select --install
brew install qt cmake
```

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

#### Linux

**Prerequisites (Debian/Ubuntu):**

```bash
sudo apt install cmake build-essential qt6-base-dev libgl1-mesa-dev
```

**Prerequisites (Fedora/RHEL):**

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel mesa-libGL-devel
```

**Prerequisites (Arch Linux):**

```bash
sudo pacman -S cmake base-devel qt6-base mesa
```

```bash
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
├── LibarchiveEngine — parameterized with format/filter registration
│   functions for 7z, RAR, RAR5, ISO, CAB, LHA, XAR, CPIO, AR,
│   WARC, MTREE, compressed tars, standalone compression
├── Bit7zEngine (7-Zip DLL/SO) — extended formats,
│   7z write with AES-256, fallback for exotic formats
├── ChdEngine (libchdr) — MAME CHD disc images (CD-ROM/HDD/DVD)
├── AdfEngine (ADFlib) — Amiga Disk Files (read + write FFS)
├── FlatArchiveEngine (base class)
│   ├── WadEngine — Doom IWAD/PWAD/WAD2/WAD3
│   ├── PakEngine — Quake PACK
│   ├── GrpEngine — Duke Nukem 3D GRP
│   ├── HogEngine — Descent HOG
│   ├── VpkEngine — Valve VPK (multi-volume _dir/_001.vpk)
│   ├── GobEngine — Dark Forces / Jedi Knight GOB
│   ├── RffEngine — Blood RFF
│   ├── BigEngine — EA games BIG/VIV
│   └── PodEngine — Terminal Velocity POD
├── MpqEngine (StormLib) — Blizzard MPQ
│   (Warcraft III, StarCraft II, Diablo III, WoW)
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
zipfx create output.adf file.txt                   # ADF floppy image
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

> **Note:** Most translations are machine-generated and may contain
> inaccuracies. If you spot an error or want to improve a translation,
> please open a pull request — human contributions are very welcome!

---

## Acknowledgements

- [Qt](https://www.qt.io/) — cross-platform UI framework
- [libzip](https://libzip.org/) — ZIP archive library
- [libarchive](https://www.libarchive.org/) — multi-format archive library
- [zlib](https://zlib.net/) — compression library
- [bzip2](https://sourceware.org/bzip2/) — bzip2 compression library
- [xz-utils / liblzma](https://tukaani.org/xz/) — xz/lzma compression library
- [Zstandard](https://facebook.github.io/zstd/) by Facebook — fast compression library
- [LZ4](https://lz4.org/) by Yann Collet — extremely fast compression library
- [Bit7z](https://github.com/rikyoz/bit7z) — 7-Zip engine C++ wrapper
- [7-Zip](https://www.7-zip.org/) by Igor Pavlov — 7z compression engine
- [p7zip](https://github.com/p7zip-project/p7zip) — 7-Zip port for POSIX systems
- [ADFlib](https://github.com/adflib/ADFlib) — Amiga Disk File library
- [StormLib](https://github.com/ladislav-zezula/StormLib) by Ladislav Zezula — Blizzard MPQ archive library
- [libchdr](https://github.com/rtissera/libchdr) — MAME Compressed Hunks of Data library
- [CLI11](https://github.com/CLIUtils/CLI11) — command-line parser

---

## License

GPLv3 — see [LICENSE](LICENSE).
