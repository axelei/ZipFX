#include "TempCleanup.h"

#include <vector>
#include <mutex>
#include <system_error>

static std::vector<fs::path>& paths()
{
    static std::vector<fs::path> s_paths;
    return s_paths;
}

static std::mutex& mutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

void TempCleanup::registerPath(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(mutex());
    paths().push_back(path);
}

void TempCleanup::unregisterPath(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(mutex());
    auto& p = paths();
    for (auto it = p.begin(); it != p.end(); ++it)
    {
        if (*it == path)
        {
            p.erase(it);
            return;
        }
    }
}

void TempCleanup::cleanupAll()
{
    std::lock_guard<std::mutex> lock(mutex());
    auto& p = paths();
    for (const auto& path : p)
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    p.clear();
}
