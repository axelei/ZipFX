#pragma once
#include "ArchiveEngine.h"
#include <atomic>

class BrotliEngine : public ArchiveEngine
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
    std::string_view FormatName() const override { return "Brotli"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& input, size_t maxBytes);

    std::string m_path;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};
};
