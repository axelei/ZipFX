#ifndef ZIPFX_RAR_ENGINE_H
#define ZIPFX_RAR_ENGINE_H

#include "ArchiveEngine.h"

struct archive;
struct archive_entry;

class RarEngine : public ArchiveEngine
{
public:
    ~RarEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool Save() override;

    bool TestIntegrity() override;

    std::string_view FormatName() const override { return "RAR"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    struct archive* m_archive = nullptr;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;

    bool LoadEntries();
};

#endif
