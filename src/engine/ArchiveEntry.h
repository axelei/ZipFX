#ifndef ZIPFX_ARCHIVE_ENTRY_H
#define ZIPFX_ARCHIVE_ENTRY_H

#include <chrono>
#include <cstdint>
#include <string>

struct ArchiveEntry
{
    std::string name;
    std::string path;
    uint64_t size = 0;
    uint64_t packedSize = 0;
    uint32_t crc = 0;
    bool isDirectory = false;
    bool isEncrypted = false;
    uint32_t permissions = 0;      // Unix mode bits (e.g., 0644)
    std::chrono::system_clock::time_point modified;
    std::string compressionMethod;
    std::string comment;
};

#endif
