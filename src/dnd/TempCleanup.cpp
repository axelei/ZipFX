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
    fs::path base = fs::weakly_canonical(fs::temp_directory_path(ec), ec);
    if (ec) return false;
    fs::path candidate = fs::weakly_canonical(path, ec);
    if (ec) return false;

    // Compare as strings with an explicit separator boundary — comparing
    // path *iterators* directly is unreliable here because
    // temp_directory_path() on Windows returns a path with a trailing
    // separator, and lexically_normal() turns that into a trailing "."
    // component that never matches a real subdirectory component.
    std::string baseStr = base.string();
    std::string candStr = candidate.string();
    if (!baseStr.empty() && (baseStr.back() == '/' || baseStr.back() == '\\'))
        baseStr.pop_back();

    if (candStr == baseStr) return true;
    if (candStr.size() <= baseStr.size()) return false;
    if (candStr.compare(0, baseStr.size(), baseStr) != 0) return false;
    char sep = candStr[baseStr.size()];
    return sep == '/' || sep == '\\';
}

void TempCleanup::registerPath(const fs::path& path)
{
    if (!isUnderTempDir(path))
    {
        std::fprintf(stderr,
            "TempCleanup: refusing to register '%s' - not under temp directory\n",
            path.string().c_str());
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
                "TempCleanup: skipping '%s' - not under temp directory\n",
                path.string().c_str());
            continue;
        }
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    p.clear();
}
