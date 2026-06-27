#include "ArchiveEngine.h"

#include "Logging.h"

#include <atomic>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::atomic<uint64_t> s_renameCounter{0};

bool ArchiveEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    auto data = ReadFile(entryName);

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out)
    {
        return false;
    }

    if (!data.empty())
    {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return out.good();
}

std::vector<uint8_t> ArchiveEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes)
        data.resize(maxBytes);
    return data;
}

bool ArchiveEngine::ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer)
{
    auto data = ReadFile(entryName);
    if (data.empty()) return true;
    return consumer(data.data(), data.size());
}

bool ArchiveEngine::RenameEntry(std::string_view entryName, std::string_view newName)
{
    // Default implementation: read, add new, remove old, save
    auto data = ReadFile(entryName);
    if (data.empty()) return false;

    // Write to temp file
    auto tmpPath = fs::temp_directory_path() /
        ("zipfx-rename-" + std::to_string(time(nullptr)) + "-" +
         std::to_string(s_renameCounter.fetch_add(1)));
    {
        std::ofstream tmp(tmpPath, std::ios::binary);
        if (!tmp) return false;
        tmp.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!tmp) { fs::remove(tmpPath); return false; }
    }

    if (!AddFile(tmpPath.string(), newName))
    {
        fs::remove(tmpPath);
        return false;
    }

    if (!RemoveEntry(entryName))
    {
        fs::remove(tmpPath);
        return false;
    }

    bool saved = Save();
    fs::remove(tmpPath);
    return saved;
}
