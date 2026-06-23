#ifndef ZIPFX_BIT7Z_ENGINE_H
#define ZIPFX_BIT7Z_ENGINE_H

#include "ArchiveEngine.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

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

    std::vector<ArchiveEntry> ListContents() override;
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

    std::string_view FormatName() const override { return "Bit7z"; }
    bool SupportsCreation() const override { return m_lib != nullptr; }
    bool IsOpen() const override { return m_isOpen; }

    // Settings (call before Create/Save)
    void setPassword(const std::string& pwd) { m_password = pwd; }
    void setEncryptHeaders(bool enc) { m_encryptHeaders = enc; }
    void setVolumeSize(uint64_t bytes) { m_volumeSize = bytes; }

    // Cancellation
    void cancelExtract() { m_extractCancelled = true; }
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
    std::string m_password;
    bool m_encryptHeaders = false;
    uint64_t m_volumeSize = 0;

    // Extraction cancellation
    std::atomic<bool> m_extractCancelled{false};
};

#endif
