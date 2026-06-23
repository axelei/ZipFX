#ifndef ZIPFX_ARCHIVE_ENGINE_FACTORY_H
#define ZIPFX_ARCHIVE_ENGINE_FACTORY_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ArchiveEngine;

class ArchiveEngineFactory
{
public:
    static std::unique_ptr<ArchiveEngine> CreateForFile(std::string_view path);
    static std::unique_ptr<ArchiveEngine> CreateForFormat(std::string_view format);
    static std::vector<std::string> SupportedExtensions();

    // If path points to a multi-volume part (e.g. .002, .part2.rar),
    // returns the first-volume path. Otherwise returns path unchanged.
    static std::string ResolveFirstVolume(std::string_view path);
};

#endif
