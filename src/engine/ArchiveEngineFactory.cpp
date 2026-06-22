#include "ArchiveEngineFactory.h"

#include "ZipEngine.h"
#include "TarGzEngine.h"
#include "SevenZipEngine.h"
#include "RarEngine.h"
#include "Rar5Engine.h"
#include "IsoEngine.h"
#include "CabEngine.h"
#include "LhaEngine.h"
#include "XarEngine.h"
#include "CpioEngine.h"
#include "Bit7zEngine.h"
#include "FileSignature.h"

#include <algorithm>
#include <cctype>
#include <string_view>

static void ToLowerInPlace(std::string& s)
{
    for (auto& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

static std::string_view Extension(std::string_view path)
{
    auto pos = path.rfind('.');
    if (pos == std::string_view::npos) return {};
    return path.substr(pos);
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFile(
    std::string_view path)
{
    // Detect by magic number first
    ArchiveType sig = FileSignature::Detect(path);
    switch (sig)
    {
    case ArchiveType::Zip:      return std::make_unique<ZipEngine>();
    case ArchiveType::SevenZip: return std::make_unique<SevenZipEngine>();
    case ArchiveType::Rar:      return std::make_unique<RarEngine>();
    case ArchiveType::Rar5:     return std::make_unique<Rar5Engine>();
    case ArchiveType::Cab:      return std::make_unique<CabEngine>();
    case ArchiveType::Lha:      return std::make_unique<LhaEngine>();
    case ArchiveType::Xar:      return std::make_unique<XarEngine>();
    case ArchiveType::Cpio:     return std::make_unique<CpioEngine>();
    case ArchiveType::Gzip:
        return std::make_unique<TarGzEngine>();
    default: break;
    }

    // Fall back to extension-based detection
    std::string ext(Extension(path));
    ToLowerInPlace(ext);

    if (ext == ".zip")   return std::make_unique<ZipEngine>();
    if (ext == ".7z")    return std::make_unique<SevenZipEngine>();
    if (ext == ".rar")   return std::make_unique<RarEngine>();
    if (ext == ".iso")   return std::make_unique<IsoEngine>();
    if (ext == ".cab")   return std::make_unique<CabEngine>();
    if (ext == ".lzh" || ext == ".lha")
        return std::make_unique<LhaEngine>();
    if (ext == ".xar")   return std::make_unique<XarEngine>();
    if (ext == ".cpio")  return std::make_unique<CpioEngine>();
    if (ext == ".tar" || ext == ".tgz" || ext == ".gz")
        return std::make_unique<TarGzEngine>();

    if (path.size() > 7)
    {
        std::string doubleExt(path.substr(path.size() - 7));
        ToLowerInPlace(doubleExt);
        if (doubleExt == ".tar.gz")
            return std::make_unique<TarGzEngine>();
    }

    // Last resort: Bit7z auto-detection for formats not covered above
    {
        auto bit7zEngine = std::make_unique<Bit7zEngine>();
        if (bit7zEngine->Open(path))
            return bit7zEngine;
    }

    return nullptr;
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFormat(
    std::string_view format)
{
    std::string fmt(format);
    ToLowerInPlace(fmt);

    if (fmt == "zip")   return std::make_unique<ZipEngine>();
    if (fmt == "7z")    return std::make_unique<SevenZipEngine>();
    if (fmt == "rar")   return std::make_unique<RarEngine>();
    if (fmt == "rar5")  return std::make_unique<Rar5Engine>();
    if (fmt == "iso")   return std::make_unique<IsoEngine>();
    if (fmt == "cab")   return std::make_unique<CabEngine>();
    if (fmt == "lha" || fmt == "lzh")
        return std::make_unique<LhaEngine>();
    if (fmt == "xar")   return std::make_unique<XarEngine>();
    if (fmt == "cpio")  return std::make_unique<CpioEngine>();
    if (fmt == "tar" || fmt == "tgz" || fmt == "tar.gz")
        return std::make_unique<TarGzEngine>();

    return nullptr;
}

std::vector<std::string> ArchiveEngineFactory::SupportedExtensions()
{
    return {".zip", ".7z", ".rar", ".iso", ".cab", ".lzh", ".lha",
            ".xar", ".cpio", ".tar", ".tgz", ".tar.gz"};
}
