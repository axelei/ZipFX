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
#include "LibarchiveEngine.h"
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

    // ── Libarchive-based engines ──────────────────────
    { "7z",     ".7z",                               ArchiveType::SevenZip,
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
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_rar,
                archive_read_support_format_rar5 },
            "RAR"); },                                                false },
    { "ISO",    ".iso",                              ArchiveType::Iso,
        []() { return std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_iso9660 },
            "ISO"); },                                                false },
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
    { "ADF",    ".adf,.adz",                         ArchiveType::Adf,
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

    // Check double extension .tar.gz
    if (path.size() > 7)
    {
        std::string doubleExt(path.substr(path.size() - 7));
        toLowerInPlace(doubleExt);
        if (doubleExt == ".tar.gz")
            return kFormats[1].create(); // TAR.GZ
    }

    for (const auto& fmt : kFormats)
    {
        if (extMatch(fmt.extensions, ext))
            return fmt.create();
    }

    // Last resort: Bit7z
    return kFormats[std::size(kFormats) - 1].create();
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
