#include "ArchiveEngineFactory.h"

#include "ZipEngine.h"
#include "TarGzEngine.h"
#include "Bit7zEngine.h"
#include "AdfEngine.h"
#include "WadEngine.h"
#include "PakEngine.h"
#include "GrpEngine.h"
#include "HogEngine.h"
#include "VpkEngine.h"
#include "MpqEngine.h"
#include "GobEngine.h"
#include "RffEngine.h"
#include "BigEngine.h"
#include "PodEngine.h"
#include "ChdEngine.h"
#include "CdiEngine.h"
#include "GdiEngine.h"
#include "IsoEngine.h"
#include "DskEngine.h"
#include "SsdEngine.h"
#include "AtrEngine.h"
#include "D64Engine.h"
#include "FatEngine.h"
#include "LibarchiveEngine.h"
#include "RarEngine.h"
#include "BsaEngine.h"
#include "BrotliEngine.h"
#include "ModEngine.h"
#include "DmgEngine.h"
#include "FileSignature.h"

#include <archive.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

#include <filesystem>
namespace fs = std::filesystem;

namespace {

struct FormatEntry
{
    const char* name;
    const char* extensions;        // comma-separated, e.g. ".lzh,.lha"
    ArchiveType magicType;         // ArchiveType for magic detection, or Unknown
    std::function<std::unique_ptr<ArchiveEngine>()> create;
    bool supportsCreation;
};

// Helper: registry of all supported archive formats
static const FormatEntry kFormats[] = {
    // ── Native engines ─────────────────────────────────
    { "ZIP",    ".zip,.jar,.apk,.docx,.xlsx,.pptx,.odt,.ods,.odp,.epub,.war,.ear",
                                                    ArchiveType::Zip,
        []() { return std::make_unique<ZipEngine>(); },              true  },
    { "TAR.GZ", ".tar,.tgz,.gz,.tar.gz",             ArchiveType::Gzip,
        []() { return std::make_unique<TarGzEngine>(); },            true  },

    // ── Compressed tar variants (via libarchive) ─────
    { "TAR.BZ2", ".tar.bz2,.tbz2,.tbz",             ArchiveType::Bzip2,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_bzip2 },
            "TAR.BZ2", false, "bzip2"); },                           false },
    { "TAR.XZ", ".tar.xz,.txz",                     ArchiveType::Xz,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_xz },
            "TAR.XZ", false, "xz"); },                               false },
    { "TAR.ZST", ".tar.zst,.tzst",                  ArchiveType::Zstd,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_zstd },
            "TAR.ZST", false, "zstd"); },                            false },
    { "TAR.LZ4", ".tar.lz4",                        ArchiveType::Lz4,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_lz4 },
            "TAR.LZ4", false, "lz4"); },                             false },
    { "TAR.LZMA", ".tar.lzma",                      ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_lzma },
            "TAR.LZMA", false, "lzma"); },                           false },

    // ── Unix compress / Lzip / Lzop (via libarchive) ─
    { "COMPRESS", ".Z",                              ArchiveType::UnixCompress,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_compress },
            "COMPRESS", false, "LZW"); },                              false },
    { "LZIP",   ".lz",                               ArchiveType::Lzip,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_lzip },
            "LZIP", false, "lzip"); },                                 false },
    { "LZOP",   ".lzo",                              ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_lzop },
            "LZOP", false, "LZO"); },                                  false },
    { "TAR.LZO", ".tar.lzo,.tzo",                   ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_tar,
                archive_read_support_format_raw,
                archive_read_support_filter_lzop },
            "TAR.LZO", false, "LZO"); },                               false },

    // ── Standalone compression (single-file) ─────────
    { "BZ2",    ".bz2",                              ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_bzip2 },
            "BZ2", false, "bzip2"); },                               false },
    { "XZ",     ".xz",                               ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_xz },
            "XZ", false, "xz"); },                                   false },
    { "ZST",    ".zst,.zstd",                        ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_zstd },
            "ZST", false, "zstd"); },                                false },
    { "LZ4",    ".lz4",                              ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_lz4 },
            "LZ4", false, "lz4"); },                                 false },
    { "LZMA",   ".lzma",                             ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_raw,
                archive_read_support_filter_lzma },
            "LZMA", false, "lzma"); },                               false },

    // ── Libarchive-based engines ──────────────────────
    { "7z",     ".7z,.001,.002,.003,.004,.005,.006,.007,.008,.009",  ArchiveType::SevenZip,
        []() {
            // Prefer Bit7zEngine if 7z.dll is available (full write support)
            auto bit7z = std::make_unique<Bit7zEngine>();
            if (bit7z->isLibraryLoaded())
                return std::unique_ptr<ArchiveEngine>(std::move(bit7z));
            // Fallback to libarchive (read-only)
            return std::unique_ptr<ArchiveEngine>(
                std::make_unique<LibarchiveEngine>(
                    std::vector<LibarchiveEngine::FormatRegistrar>{
                        archive_read_support_format_7zip },
                    "7z", true, "LZMA2"));
        },                                                           true  },
    { "RAR",    ".rar",                              ArchiveType::Rar,
        []() { return std::make_unique<RarEngine>(); },
        // RarEngine::SupportsCreation() returns true only when rar.exe is found
        true },
    { "ARJ",    ".arj",                              ArchiveType::Arj,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            e->setReadOnly(true);
            return std::unique_ptr<ArchiveEngine>(std::move(e));
        },                                                                false },
    { "ISO",    ".iso",                              ArchiveType::Iso,
        []() { return std::make_unique<IsoEngine>(); },              false },
    { "CAB",    ".cab",                              ArchiveType::Cab,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_cab },
            "CAB"); },                                                false },
    { "LHA",    ".lzh,.lha",                         ArchiveType::Lha,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_lha },
            "LHA"); },                                                false },
    { "XAR",    ".xar",                              ArchiveType::Xar,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_xar },
            "XAR"); },                                                false },
    { "CPIO",   ".cpio",                             ArchiveType::Cpio,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_cpio },
            "CPIO"); },                                               false },
    { "AR",     ".a,.deb",                           ArchiveType::Ar,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_ar },
            "AR"); },                                                false },
    { "WARC",   ".warc",                             ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_warc },
            "WARC"); },                                              false },
    { "MTREE",  ".mtree",                            ArchiveType::Unknown,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_mtree },
            "MTREE"); },                                             false },
    { "ADF",    ".adf,.adz,.hdf",                    ArchiveType::Adf,
        []() { return std::make_unique<AdfEngine>(); },              false },

    // ── Game archive formats (flat, no compression) ──
    { "WAD",    ".wad",                              ArchiveType::Wad,
        []() { return std::make_unique<WadEngine>(); },               false },
    { "PAK",    ".pak",                              ArchiveType::Pak,
        []() { return std::make_unique<PakEngine>(); },               false },
    { "GRP",    ".grp",                              ArchiveType::Grp,
        []() { return std::make_unique<GrpEngine>(); },               false },
    { "HOG",    ".hog",                              ArchiveType::Hog,
        []() { return std::make_unique<HogEngine>(); },               false },
    { "VPK",    ".vpk",                              ArchiveType::Vpk,
        []() { return std::make_unique<VpkEngine>(); },                 false },
    { "GOB",    ".gob",                              ArchiveType::Gob,
        []() { return std::make_unique<GobEngine>(); },               false },
    { "RFF",    ".rff",                              ArchiveType::Rff,
        []() { return std::make_unique<RffEngine>(); },               false },
    { "BIG",    ".big,.viv",                         ArchiveType::Big,
        []() { return std::make_unique<BigEngine>(); },               false },
    { "POD",    ".pod",                              ArchiveType::Pod,
        []() { return std::make_unique<PodEngine>(); },               false },
    { "MPQ",    ".mpq,.mpk,.w3x,.w3m",               ArchiveType::Mpq,
        []() { return std::make_unique<MpqEngine>(); },                  true  },
    { "BSA",    ".bsa",                              ArchiveType::Bsa,
        []() { return std::make_unique<BsaEngine>(); },                  false },
    { "Brotli", ".br",                               ArchiveType::Unknown,
        []() { return std::make_unique<BrotliEngine>(); },               false },

    // ── Apple Disk Image ────────────────────────────────
    { "DMG",    ".dmg",                              ArchiveType::Dmg,
        []() {
#ifdef __APPLE__
            return std::unique_ptr<ArchiveEngine>(std::make_unique<DmgEngine>());
#else
            auto e = std::make_unique<Bit7zEngine>();
            e->setReadOnly(true);
            return std::unique_ptr<ArchiveEngine>(std::move(e));
#endif
        },                                                                false },

    // ── Disc images (compressed / raw) ────────────────
    { "CDI",    ".cdi",                              ArchiveType::Cdi,
        []() { return std::make_unique<CdiEngine>(); },               false },
    { "ISO",    ".iso",                              ArchiveType::Iso,
        []() { return std::make_unique<IsoEngine>(); },              false },
    { "GDI",    ".gdi",                              ArchiveType::Gdi,
        []() { return std::make_unique<GdiEngine>(); },               false },
    // ── Retro disk image formats ──────────────────────────────
    { "SSD",    ".ssd,.dsd",                         ArchiveType::Unknown,
        []() { return std::make_unique<SsdEngine>(); },               false },
    { "ATR",    ".atr",                              ArchiveType::Atr,
        []() { return std::make_unique<AtrEngine>(); },               false },
    { "D64",    ".d64,.d71",                         ArchiveType::D64,
        []() { return std::make_unique<D64Engine>(); },               false },
    { "DSK",    ".dsk,.d80,.d82,.td0,.imd,.dc42,.2mg",
                                                     ArchiveType::Dsk,
        []() { return std::make_unique<DskEngine>(); },               false },
    { "CHD",    ".chd",                              ArchiveType::Chd,
        []() { return std::make_unique<ChdEngine>(); },               false },

    // ── Disk images (via Bit7z) ───────────────────────
    // These need 7z.dll; explicit entries give proper FormatName display.
    { "VHD",    ".vhd,.vhdx",                        ArchiveType::Vhd,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            if (e->isLibraryLoaded()) return std::unique_ptr<ArchiveEngine>(std::move(e));
            return std::unique_ptr<ArchiveEngine>(std::make_unique<LibarchiveEngine>(
                std::vector<LibarchiveEngine::FormatRegistrar>{}, "VHD"));
        },                                                                false },
    { "VMDK",   ".vmdk",                             ArchiveType::Vmdk,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            if (e->isLibraryLoaded()) return std::unique_ptr<ArchiveEngine>(std::move(e));
            return std::unique_ptr<ArchiveEngine>(std::make_unique<LibarchiveEngine>(
                std::vector<LibarchiveEngine::FormatRegistrar>{}, "VMDK"));
        },                                                                false },
    { "QCOW",   ".qcow,.qcow2",                      ArchiveType::Qcow,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            if (e->isLibraryLoaded()) return std::unique_ptr<ArchiveEngine>(std::move(e));
            return std::unique_ptr<ArchiveEngine>(std::make_unique<LibarchiveEngine>(
                std::vector<LibarchiveEngine::FormatRegistrar>{}, "QCOW"));
        },                                                                false },
    { "NRG",    ".nrg",                              ArchiveType::Nrg,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            if (e->isLibraryLoaded()) return std::unique_ptr<ArchiveEngine>(std::move(e));
            return std::unique_ptr<ArchiveEngine>(std::make_unique<LibarchiveEngine>(
                std::vector<LibarchiveEngine::FormatRegistrar>{}, "NRG"));
        },                                                                false },
    { "BIN/CUE",".bin,.cue",                         ArchiveType::Unknown,
        []() {
            auto e = std::make_unique<Bit7zEngine>();
            if (e->isLibraryLoaded()) return std::unique_ptr<ArchiveEngine>(std::move(e));
            return std::unique_ptr<ArchiveEngine>(std::make_unique<LibarchiveEngine>(
                std::vector<LibarchiveEngine::FormatRegistrar>{}, "BIN/CUE"));
        },                                                                false },
    { "FAT12 Floppy", ".st,.vfd,.img,.ima",             ArchiveType::Fat,
        []() { return std::make_unique<FatEngine>(); },               false },
    { "Disk",   ".flp",                               ArchiveType::Unknown,
        []() { return std::make_unique<Bit7zEngine>(); },                   false },

    // ── Tracker modules (read-only) ────────────────────
    { "Tracker", ".mod,.s3m,.it,.xm,.med,.mtm,.669,.ult,.stm",
                                                     ArchiveType::Mod,
        []() { return std::make_unique<ModEngine>(); },              false },

    // ── Bit7z fallback (last resort) ──────────────────
    { "Bit7z",  nullptr,                             ArchiveType::Unknown,
        []() { return std::make_unique<Bit7zEngine>(); },             false },
};

static void toLowerInPlace(std::string& s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static bool extMatch(const char* pattern, const std::string& ext)
{
    if (!pattern) return false;
    const char* p = pattern;
    while (*p)
    {
        const char* end = std::strchr(p, ',');
        if (!end) end = p + std::strlen(p);
        if (static_cast<size_t>(end - p) == ext.size() &&
            std::equal(p, end, ext.begin(),
                [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
            return true;
        p = end + (*end ? 1 : 0);
    }
    return false;
}

} // anonymous namespace

std::string ArchiveEngineFactory::ResolveFirstVolume(std::string_view path)
{
    std::string spath(path);
    auto dot = spath.rfind('.');
    if (dot == std::string::npos) return spath;

    std::string ext = spath.substr(dot + 1);

    // Pattern: .002-.999 → .001  (7z/zip split volumes)
    if (ext.size() == 3 && ext.find_first_not_of("0123456789") == std::string::npos)
    {
        int volNum = std::stoi(ext);
        if (volNum > 1)
        {
            // Check for .7z.001 / .zip.001 naming
            auto prevDot = spath.rfind('.', dot - 1);
            if (prevDot != std::string::npos && (dot - prevDot) <= 5)
            {
                std::string innerExt = spath.substr(prevDot + 1, dot - prevDot - 1);
                std::string firstVol = spath.substr(0, prevDot + 1) + innerExt + ".001";
                if (fs::exists(firstVol))
                    return firstVol;
            }
            std::string firstVol = spath.substr(0, dot) + ".001";
            if (fs::exists(firstVol))
                return firstVol;
        }
        return spath;
    }

    // Pattern: .part2.rar, .part3.zip → .part1.*
    auto partPos = spath.rfind(".part");
    if (partPos != std::string::npos)
    {
        auto numEnd = spath.find('.', partPos + 6);
        if (numEnd != std::string::npos)
        {
            std::string numStr = spath.substr(partPos + 5, numEnd - partPos - 5);
            if (!numStr.empty() &&
                numStr.find_first_not_of("0123456789") == std::string::npos &&
                std::stoi(numStr) > 1)
            {
                std::string firstVol = spath.substr(0, partPos + 5) + "1" + spath.substr(numEnd);
                if (fs::exists(firstVol))
                    return firstVol;
            }
        }
    }

    return spath;
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFile(
    std::string_view path)
{
    // Magic-number detection
    ArchiveType sig = FileSignature::Detect(path);
    if (sig != ArchiveType::Unknown)
    {
        for (const auto& fmt : kFormats)
        {
            if (fmt.magicType == sig)
                return fmt.create();
        }
    }

    // Extension-based detection
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos)
        return kFormats[std::size(kFormats) - 1].create(); // try Bit7z

    std::string ext(path.substr(dot));
    toLowerInPlace(ext);

    // Check double extensions for compressed tars
    static const struct { const char* suffix; const char* formatName; } kDoubleExts[] = {
        { ".tar.gz",   "TAR.GZ"   },
        { ".tar.bz2",  "TAR.BZ2"  },
        { ".tar.xz",   "TAR.XZ"   },
        { ".tar.zst",  "TAR.ZST"  },
        { ".tar.lz4",  "TAR.LZ4"  },
        { ".tar.lzma", "TAR.LZMA" },
        { ".tar.lzo",  "TAR.LZO"  },
    };
    for (const auto& de : kDoubleExts)
    {
        size_t suffixLen = std::strlen(de.suffix);
        if (path.size() > suffixLen)
        {
            std::string suffix(path.substr(path.size() - suffixLen));
            toLowerInPlace(suffix);
            if (suffix == de.suffix)
            {
                for (const auto& fmt : kFormats)
                    if (std::string_view(fmt.name) == de.formatName)
                        return fmt.create();
            }
        }
    }

    for (const auto& fmt : kFormats)
    {
        if (extMatch(fmt.extensions, ext))
            return fmt.create();
    }

    // Last resort: Bit7z (only if the library is actually available)
    {
        auto bit7z = std::make_unique<Bit7zEngine>();
        if (bit7z->isLibraryLoaded())
            return bit7z;
    }

    return nullptr;
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFormat(
    std::string_view format)
{
    std::string fmt(format);
    toLowerInPlace(fmt);

    for (const auto& entry : kFormats)
    {
        std::string name(entry.name);
        toLowerInPlace(name);
        if (name == fmt)
            return entry.create();
    }
    return nullptr;
}

std::vector<std::string> ArchiveEngineFactory::SupportedExtensions()
{
    std::vector<std::string> result;
    for (const auto& fmt : kFormats)
    {
        if (!fmt.extensions) continue;
        const char* p = fmt.extensions;
        while (*p)
        {
            const char* end = std::strchr(p, ',');
            if (!end) end = p + std::strlen(p);
            result.emplace_back(p, end);
            p = end + (*end ? 1 : 0);
        }
    }
    return result;
}
