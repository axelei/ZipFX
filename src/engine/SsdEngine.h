#ifndef ZIPFX_SSD_ENGINE_H
#define ZIPFX_SSD_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Read-only engine for Acorn BBC Micro DFS disk images (.ssd, .dsd).
// SSD = single-sided (40 or 80 tracks × 10 sectors × 256 bytes).
// DSD = double-sided (interleaved, same catalog layout).
class SsdEngine : public ArchiveEngine
{
public:
    ~SsdEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;
    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes) override;
    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    void cancelExtract() override { m_extractCancelled = true; }

    std::string_view FormatName() const override { return m_formatName; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    struct SsdEntry {
        std::string name;
        uint32_t    startSector = 0;
        uint32_t    size        = 0;
    };

    std::vector<SsdEntry>     m_ssdEntries;
    std::vector<uint8_t>      m_diskData;
    std::vector<ArchiveEntry> m_entries;
    std::string               m_path;
    std::string               m_formatName = "SSD";
    bool                      m_isOpen     = false;
    std::atomic<bool>         m_extractCancelled{false};
};

#endif
