#include "ArchiveEngineFactory.h"

#include "ZipEngine.h"
#include "TarGzEngine.h"
#include "SevenZipEngine.h"
#include "RarEngine.h"

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
    if (pos == std::string_view::npos)
    {
        return {};
    }
    return path.substr(pos);
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFile(
    std::string_view path)
{
    std::string ext(Extension(path));
    ToLowerInPlace(ext);

    if (ext == ".zip")
    {
        return std::make_unique<ZipEngine>();
    }
    if (ext == ".7z")
    {
        return std::make_unique<SevenZipEngine>();
    }
    if (ext == ".rar")
    {
        return std::make_unique<RarEngine>();
    }
    if (ext == ".tar" || ext == ".tgz" || ext == ".gz")
    {
        // .tgz is tar.gz; .gz alone is gzip; treat as tar.gz
        return std::make_unique<TarGzEngine>();
    }

    // Check for .tar.gz double extension
    if (path.size() > 7)
    {
        std::string doubleExt(path.substr(path.size() - 7));
        ToLowerInPlace(doubleExt);
        if (doubleExt == ".tar.gz")
        {
            return std::make_unique<TarGzEngine>();
        }
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
    if (fmt == "tar" || fmt == "tgz" || fmt == "tar.gz")
        return std::make_unique<TarGzEngine>();

    return nullptr;
}

std::vector<std::string> ArchiveEngineFactory::SupportedExtensions()
{
    return {".zip", ".7z", ".rar", ".tar", ".tgz", ".tar.gz"};
}
