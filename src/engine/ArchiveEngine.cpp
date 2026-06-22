#include "ArchiveEngine.h"

#include "Logging.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

bool ArchiveEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    auto data = ReadFile(entryName);
    if (data.empty())
    {
        return false;
    }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out)
    {
        return false;
    }

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}
