#include "TempCleanup.h"

#include <vector>
#include <mutex>
#include <system_error>
#include <cstdio>

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

static bool isUnderTempDir(const fs::path& path)
{
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) return false;
    // Require the candidate to start with the system temp dir.
    auto b = base.lexically_normal();
    auto p = path.lexically_normal();
    auto [bEnd, pIt] = std::mismatch(b.begin(), b.end(), p.begin());
    return bEnd == b.end() && pIt != p.begin();
}

void TempCleanup::registerPath(const fs::path& path)
{
    if (!isUnderTempDir(path))
    {
        std::fprintf(stderr,
            "TempCleanup: refusing to register '%s' — not under temp directory\n",
            path.c_str());
        return;
    }
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
        if (!isUnderTempDir(path))
        {
            std::fprintf(stderr,
                "TempCleanup: skipping '%s' — not under temp directory\n",
                path.c_str());
            continue;
        }
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    p.clear();
}
