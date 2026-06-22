#ifndef ZIPFX_FLAT_ARCHIVE_ENGINE_H
#define ZIPFX_FLAT_ARCHIVE_ENGINE_H

#include "ArchiveEngine.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <fstream>

class FlatArchiveEngine : public ArchiveEngine
{
public:
    ~FlatArchiveEngine() override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

protected:
    struct FileEntry { std::string name; uint32_t offset = 0; uint32_t size = 0; };

    bool parse(std::string_view path, const char* formatName,
               std::function<bool(std::ifstream& f, FileEntry& entry)> readEntry);

    bool parse(std::string_view path, const char* formatName,
               std::function<bool(std::ifstream& f, std::vector<FileEntry>& entries)> readHeader);

    std::string m_formatName;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<FileEntry> m_entries;

private:
    int findEntry(std::string_view name) const;
};

#endif
