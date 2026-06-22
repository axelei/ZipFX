#ifndef ZIPFX_ARCHIVE_ENGINE_H
#define ZIPFX_ARCHIVE_ENGINE_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ArchiveEntry.h"

class ArchiveEngine
{
public:
    virtual ~ArchiveEngine() = default;

    virtual bool Open(std::string_view path) = 0;
    virtual bool Create(std::string_view path) { return false; }
    virtual void Close() = 0;

    virtual std::vector<ArchiveEntry> ListContents() = 0;
    virtual bool Extract(std::string_view entryName, std::string_view destPath);
    virtual bool ExtractAll(std::string_view destPath) = 0;
    virtual std::vector<uint8_t> ReadFile(std::string_view entryName) = 0;

    virtual bool AddFile(std::string_view srcPath, std::string_view archivePath) { return false; }
    virtual bool RemoveEntry(std::string_view entryName) { return false; }
    virtual bool Save() { return false; }

    virtual bool TestIntegrity() = 0;

    virtual std::string_view FormatName() const = 0;
    virtual bool SupportsCreation() const = 0;
    virtual bool IsOpen() const = 0;
};

#endif
