#ifndef ZIPFX_BIT7Z_ENGINE_H
#define ZIPFX_BIT7Z_ENGINE_H

#include "ArchiveEngine.h"

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>

namespace bit7z {
class Bit7zLibrary;
class BitArchiveReader;
}

class Bit7zEngine : public ArchiveEngine
{
public:
    Bit7zEngine();
    ~Bit7zEngine() override;

    bool Open(std::string_view path) override;
    bool Create(std::string_view path) override;
    void Close() override;

    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    bool isLibraryLoaded() const { return m_lib != nullptr; }
    void setReadOnly(bool ro) { m_readOnly = ro; }

    std::string_view FormatName() const override { return "Bit7z"; }
    bool SupportsCreation() const override { return m_lib != nullptr && !m_readOnly; }
    bool IsOpen() const override { return m_isOpen; }
    bool SupportsViewFile() const override;
    std::string ViewUnsupportedReason() const override;

    // Settings (call before Create/Save)
    std::string archiveComment() const override;

    void setPassword(std::string_view pwd) override { m_password = std::string(pwd); }
    bool SupportsEncryption() const override { return true; }
    void setEncryptHeaders(bool enc) { m_encryptHeaders = enc; }
    void setVolumeSize(uint64_t bytes) { m_volumeSize = bytes; }
    void setCompressionMethod(int method) { m_compressionMethod = method; }
    void setSolidMode(bool solid) { m_solidMode = solid; m_solidModeSet = true; }
    void setDictionarySize(uint32_t bytes) { m_dictionarySize = bytes; }
    void setWordSize(uint32_t ws) { m_wordSize = ws; }
    void setThreadsCount(uint32_t n) { m_threadsCount = n; }

    // Cancellation
    void cancelExtract() override { m_extractCancelled = true; }
    bool isExtractCancelled() const { return m_extractCancelled; }

private:
    int findEntry(std::string_view name) const;

    std::unique_ptr<bit7z::Bit7zLibrary> m_lib;
    std::unique_ptr<bit7z::BitArchiveReader> m_reader;
    std::string m_path;
    bool m_isOpen = false;
    bool m_isNew = false;
    std::vector<ArchiveEntry> m_entries;

    // Write state
    std::map<std::string, std::string> m_pendingAdds; // archivePath -> srcPath
    std::set<std::string> m_pendingDeletes;
    bool m_modified = false;
    std::string m_password;
    bool m_encryptHeaders = false;
    uint64_t m_volumeSize = 0;

    bool m_readOnly = false;

    // Compression options
    int m_compressionMethod = -1;   // -1=default; cast to BitCompressionMethod
    uint32_t m_dictionarySize = 0;  // 0=default
    uint32_t m_wordSize = 0;        // 0=default
    uint32_t m_threadsCount = 0;    // 0=auto
    bool m_solidMode = true;
    bool m_solidModeSet = false;

    // Extraction cancellation
    std::atomic<bool> m_extractCancelled{false};
};

#endif
