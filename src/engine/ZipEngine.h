#ifndef ZIPFX_ZIP_ENGINE_H
#define ZIPFX_ZIP_ENGINE_H

#include "ArchiveEngine.h"

#include <miniz.h>

class ZipEngine : public ArchiveEngine
{
public:
    ~ZipEngine() override;

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

    std::string_view FormatName() const override { return "ZIP"; }
    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_isOpen; }

private:
    mz_zip_archive m_archive = {};
    std::string m_path;
    bool m_isOpen = false;
    bool m_modified = false;
    bool m_isWriter = false;

    void ClearEntryCache();
    void LoadEntryCache();
};

#endif
