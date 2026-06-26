#pragma once
#include "ArchiveEngine.h"
#include <atomic>

class BsaEngine : public ArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    void Close() override;
    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes) override;
    bool TestIntegrity(std::function<void(int, int)> progressCallback = nullptr,
                       std::function<bool()> cancelFlag = nullptr) override;
    void cancelExtract() override { m_extractCancelled = true; }
    std::string_view FormatName() const override { return m_formatName; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    struct FileRecord {
        std::string fullPath;
        uint32_t dataStart;  // absolute offset to payload (past BSTRING + origSize field)
        uint32_t dataSize;   // bytes of payload (compressed or raw)
        uint32_t origSize;   // 0 = uncompressed; else = uncompressed size
        bool compressed;
    };

    std::vector<uint8_t> doRead(const FileRecord& rec, size_t maxBytes);

    std::string m_path;
    std::string m_formatName = "BSA";
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;
    std::vector<FileRecord> m_fileRecords;
    std::atomic<bool> m_extractCancelled{false};
};
