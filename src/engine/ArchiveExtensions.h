#pragma once

// Single source of truth for all file extensions recognised by ZipFX.
// Included by ArchiveEngineFactory.cpp, MainWindow.cpp, and ZipFXShellExt.cpp.
//
// Rules for this list:
//   - Single-segment extensions only (.gz, not .tar.gz — those live in kFormats)
//   - No overly-generic entries: .a (C static lib), .001-.009 (multi-volume)
//   - Keep sorted by format group for readability
//   - When adding a new engine, add its extensions here AND in kFormats[]
//
// Usage — narrow (char):
//   static const char* exts[] = { ZIPFX_ARCHIVE_EXTS(ZIPFX_EXT_N) nullptr };
//
// Usage — wide (wchar_t):
//   static const wchar_t* exts[] = { ZIPFX_ARCHIVE_EXTS(ZIPFX_EXT_W) nullptr };

#define ZIPFX_EXT_N(e) e,
#define ZIPFX_EXT_W(e) L##e,

// clang-format off
#define ZIPFX_ARCHIVE_EXTS(X) \
    /* ── ZIP family ──────────────────────────────── */ \
    X(".zip")   X(".jar")   X(".apk")   X(".war")   X(".ear") \
    X(".docx")  X(".xlsx")  X(".pptx") \
    X(".odt")   X(".ods")   X(".odp") \
    X(".epub") \
    /* ── Tar-compressed (single-segment) ─────────── */ \
    X(".tar")   X(".tgz")   X(".tbz2")  X(".tbz") \
    X(".txz")   X(".tzst") \
    /* ── Standalone compression ───────────────────── */ \
    X(".gz")    X(".bz2")   X(".xz") \
    X(".zst")   X(".zstd")  X(".lz4")   X(".lzma") \
    X(".lz")    X(".Z")    X(".lzo") \
    X(".br") \
    /* ── 7-Zip / RAR / ARJ ────────────────────────── */ \
    X(".7z")    X(".rar")   X(".arj") \
    /* ── Libarchive formats ────────────────────────── */ \
    X(".iso")   X(".cab") \
    X(".lzh")   X(".lha") \
    X(".xar")   X(".cpio") \
    X(".deb") \
    X(".warc")  X(".mtree") \
    /* ── Amiga / retro ────────────────────────────── */ \
    X(".adf")   X(".adz") \
    X(".ssd")   X(".dsd") \
    X(".atr") \
    X(".d64")   X(".d71") \
    X(".dsk")   X(".d80")   X(".d82") \
    X(".td0")   X(".imd")   X(".dc42")  X(".2mg") \
    /* ── Game archives ────────────────────────────── */ \
    X(".wad")   X(".pak")   X(".grp")   X(".hog") \
    X(".vpk")   X(".gob")   X(".rff") \
    X(".big")   X(".viv")   X(".pod") \
    X(".mpq")   X(".mpk")   X(".w3x")   X(".w3m") \
    X(".bsa") \
    /* ── Disc images ──────────────────────────────── */ \
    X(".chd")   X(".cdi")   X(".gdi") \
    X(".nrg")   X(".bin")   X(".cue") \
    /* ── Virtual disk images ──────────────────────── */ \
    X(".vhd")   X(".vhdx") \
    X(".vmdk") \
    X(".qcow")  X(".qcow2") \
    X(".img")   X(".ima")   X(".st")    X(".vfd")   X(".flp")
// clang-format on
