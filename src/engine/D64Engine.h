#ifndef ZIPFX_D64_ENGINE_H
#define ZIPFX_D64_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Read-only engine for Commodore 64/128 disk images (.d64, .d71).
//
// D64: 35 tracks, variable sectors per track, 174848 bytes (or 175531 with
//      error bytes).  Track 18 holds the BAM and directory.
// D71: Two D64 sides, 349696 bytes.  Tracks 1-35 = side 1 (as D64),
//      tracks 36-70 = side 2 mirror.  We parse both sides as a flat list.
class D64Engine : public ArchiveEngine
{
public:
    ~D64Engine() override;

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
    // Returns byte offset of a given track (1-indexed) in the image data.
    // Returns -1 on invalid track.
    static int trackOffset(int track);
    // Returns pointer to sector data (track 1-indexed, sector 0-indexed) or nullptr.
    const uint8_t* sectorData(int track, int sector) const;
    // Parses one D64 side starting at data offset 0.
    void parseSide(const uint8_t* base, size_t len, const std::string& prefix = {});
    // Reads a file's data by following the track/sector chain.
    std::vector<uint8_t> readFileData(const uint8_t* base, size_t len,
                                      int startTrack, int startSector) const;

    struct D64Entry {
        std::string name;
        int         startTrack;
        int         startSector;
        uint32_t    blocks;     // size in 256-byte blocks
        size_t      baseOffset; // offset within m_diskData for this side
    };

    std::vector<D64Entry>     m_d64Entries;
    std::vector<uint8_t>      m_diskData;
    std::vector<ArchiveEntry> m_entries;
    std::string               m_path;
    std::string               m_formatName = "D64";
    bool                      m_isD71      = false;
    bool                      m_isOpen     = false;
    std::atomic<bool>         m_extractCancelled{false};
};

#endif
