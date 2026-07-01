#ifndef ZIPFX_TEMP_CLEANUP_H
#define ZIPFX_TEMP_CLEANUP_H

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class TempCleanup
{
public:
    // Register a path for recursive removal on application exit.
    static void registerPath(const fs::path& path);

    // Remove a previously registered path from the cleanup list
    // (e.g., if the caller handled early cleanup itself).
    static void unregisterPath(const fs::path& path);

    // Remove all registered paths (directories are removed recursively).
    // Idempotent — safe to call even if paths were already deleted.
    static void cleanupAll();

private:
    TempCleanup() = default;
};

#endif
