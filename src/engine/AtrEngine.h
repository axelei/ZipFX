#ifndef ZIPFX_ATR_ENGINE_H
#define ZIPFX_ATR_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Read-only engine for Atari 8-bit floppy disk images (.atr).
// Supports Atari DOS 2.x filesystem: parses VTOC (sector 360) and
// directory sectors (361-368).  Handles both SD (128 bytes/sector)
// and DD (256 bytes/sector) images.
class AtrEngine : public ArchiveEngine
{
public:
    ~AtrEngine() override;

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

    std::string_view FormatName() const override { return "ATR"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    // Returns pointer to the first byte of 1-indexed sector N, or nullptr.
    const uint8_t* sectorPtr(uint32_t sectorNum) const;
    // Returns byte offset of 1-indexed sector N in m_diskData.
    uint64_t sectorOffset(uint32_t sectorNum) const;
    // Reads a full sector, returning its bytes (bps or 128 for sectors 1-3).
    std::vector<uint8_t> readSector(uint32_t sectorNum) const;
    // Follows the DOS 2.x sector chain starting at firstSector.
    std::vector<uint8_t> readChain(uint16_t firstSector) const;

    struct AtrEntry {
        std::string name;        // "FILENAME.EXT"
        uint16_t    firstSector; // 1-indexed
        uint16_t    sectorCount;
        uint32_t    size;        // approximate: sectorCount * (bps - 3)
    };

    std::vector<AtrEntry>     m_atrEntries;
    std::vector<uint8_t>      m_diskData;  // image data including 16-byte header
    std::vector<ArchiveEntry> m_entries;
    std::string               m_path;
    uint16_t                  m_bps       = 128; // bytes per sector
    bool                      m_isOpen    = false;
    std::atomic<bool>         m_extractCancelled{false};
};

#endif
