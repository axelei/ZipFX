#ifndef ZIPFX_ZIP_ENGINE_H
#define ZIPFX_ZIP_ENGINE_H

#include "ArchiveEngine.h"

#include <zip.h>

#include <string>
#include <vector>

class ZipEngine : public ArchiveEngine
{
public:
    ~ZipEngine() override;

    bool Open(std::string_view path) override;
    bool Create(std::string_view path) override;
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
    bool IsOpen() const override { return m_zip != nullptr; }

private:
    zip_t*  m_zip = nullptr;
    std::string m_path;
    bool    m_modified = false;
    bool    m_isNew = false; // created via Create(), not opened

    std::vector<ArchiveEntry> m_entries;

    struct PendingAdd
    {
        std::string srcPath;
        std::string archivePath;
    };
    std::vector<PendingAdd> m_pendingAdds;

    void LoadEntries();
    static int GetCompressionLevel();
};

#endif
