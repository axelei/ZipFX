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

    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    void cancelExtract() override { m_extractCancelled = true; }

    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_isOpen; }

    bool Create(std::string_view path) override;
    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool RenameEntry(std::string_view entryName, std::string_view newName) override;
    bool Save() override;

protected:
    struct FileEntry { std::string name; uint32_t offset = 0; uint32_t size = 0; std::vector<uint8_t> data; uint16_t archiveIndex = 0x7FFF; };

    bool parse(std::string_view path, const char* formatName,
               std::function<bool(std::ifstream& f, FileEntry& entry)> readEntry);

    bool parse(std::string_view path, const char* formatName,
               std::function<bool(std::ifstream& f, std::vector<FileEntry>& entries)> readHeader);

    // Subclasses override to write their format on Save()
    virtual bool doSave(std::ofstream& f) = 0;

    void rebuildArchiveEntries();

    std::string m_formatName;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<FileEntry> m_entries;
    std::vector<ArchiveEntry> m_archiveEntries;
    std::atomic<bool> m_extractCancelled{false};
    int findEntry(std::string_view name) const;
};

#endif
