#include "ArchiveEngineFactory.h"

#include "ZipEngine.h"
#include "TarGzEngine.h"
#include "SevenZipEngine.h"
#include "RarEngine.h"

#include <algorithm>
#include <cctype>
#include <string_view>

static std::string_view ToLower(std::string_view s)
{
    // Simple lowercasing for ASCII extensions
    for (auto& c : const_cast<std::string&>(std::string(s)))
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
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
    auto ext = ToLower(Extension(path));

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
        auto doubleExt = path.substr(path.size() - 7);
        if (ToLower(doubleExt) == ".tar.gz")
        {
            return std::make_unique<TarGzEngine>();
        }
    }

    return nullptr;
}

std::unique_ptr<ArchiveEngine> ArchiveEngineFactory::CreateForFormat(
    std::string_view format)
{
    auto fmt = ToLower(format);

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
