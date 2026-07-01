#ifndef ZIPFX_ZIP_ENGINE_H
#define ZIPFX_ZIP_ENGINE_H

#include "ArchiveEngine.h"

#include <zip.h>

#include <memory>
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
    std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes) override;
    bool ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool RenameEntry(std::string_view entryName, std::string_view newName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string archiveComment() const override;
    bool setArchiveComment(std::string_view comment) override;
    bool setEntryComment(std::string_view entryName, std::string_view comment) override;

    void setPassword(std::string_view pwd) override { m_password = pwd; }
    bool SupportsEncryption() const override { return true; }
    void setEncryptHeaders(bool enc) { m_encryptHeaders = enc; }
    void cancelExtract() override { m_extractCancelled = true; }
    bool isExtractCancelled() const { return m_extractCancelled; }

    std::string_view FormatName() const override { return "ZIP"; }
    bool SupportsCreation() const override { return true; }
    bool supportsArchiveComment() const override { return true; }
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

    std::unique_ptr<ArchiveEngine> m_bit7zFallback; // for Deflate64 entries

    void LoadEntries();
};

#endif
