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

    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool RenameEntry(std::string_view entryName, std::string_view newName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    void setPassword(const std::string& pwd) { m_password = pwd; }
    void setEncryptHeaders(bool enc) { m_encryptHeaders = enc; }
    void cancelExtract() override { m_extractCancelled = true; }
    bool isExtractCancelled() const { return m_extractCancelled; }

    std::string_view FormatName() const override { return "ZIP"; }
    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_zip != nullptr; }

private:
    zip_t*  m_zip = nullptr;
    std::string m_path;
    bool    m_modified = false;
    bool    m_isNew = false; // created via Create(), not opened
    std::string m_password;
    bool m_encryptHeaders = false;
    std::atomic<bool> m_extractCancelled{false};

    std::vector<ArchiveEntry> m_entries;

    struct PendingAdd
    {
        std::string srcPath;
        std::string archivePath;
    };
    std::vector<PendingAdd> m_pendingAdds;

    uint64_t m_totalBytesForProgress = 0;
    std::string m_lastFileName;

    void LoadEntries();
};

#endif
