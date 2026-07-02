#ifndef ZIPFX_CDI_ENGINE_H
#define ZIPFX_CDI_ENGINE_H

#include "ArchiveEngine.h"
#include "Iso9660Reader.h"

#include <atomic>
#include <cstdint>
#include <vector>

struct archive;
struct archive_entry;

// Stream state passed to libarchive callbacks (legacy whole-file-as-one-
// track fallback path — used only when the real DiscJuggler footer can't
// be parsed or none of its tracks yield a mountable ISO 9660 filesystem).
struct CdiIsoStream {
    FILE* file = nullptr;
    uint32_t sectorSize = 2048;
    uint32_t seekHeader = 0;
    uint32_t seekEcc = 0;
    uint64_t totalSectors = 0;
    uint64_t isoPos = 0;
    std::vector<uint8_t> buf;
};

class CdiEngine : public ArchiveEngine
{
public:
    ~CdiEngine() override;

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

    std::string_view FormatName() const override { return "CDI"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    enum class Type { Normal, Raw, PQ, CdG };

    // Real DiscJuggler track, parsed from the file's footer (sessions +
    // per-track descriptors). Sectors for each track are stored back to
    // back in file order (not at "Track Start Address", which is disc/TOC
    // metadata, not a file position) — fileOffset is the byte position of
    // this track's own sector 0 as actually stored in the file.
    struct CdiTrackInfo {
        std::string name;
        int trackMode = 1;      // 0=Audio, 1=Mode1, 2=Mode2/Mixed
        int sessionNum = 0;
        int trackNum = 0;
        uint64_t fileOffset = 0;
        uint32_t sectorSize = 2336; // physical bytes per sector as stored
        uint32_t headerOff = 0;     // bytes to skip per sector to reach user data
        uint32_t userSize = 2048;   // usable bytes per sector
        uint64_t numSectors = 0;
        // Disc/TOC-relative "Track Start Address" from the footer. Not a
        // file offset (see parseFooter()), but the ISO 9660 structures
        // stored within some tracks (observed on GD-ROM-style discs, same
        // convention already handled for GD-ROM CHDs) reference LBAs that
        // are disc-absolute rather than relative to the track's own start —
        // this is the value to subtract to translate them back.
        uint32_t discStartLba = 0;
    };

    bool detectType();
    bool ensureArchiveOpen();
    bool loadAllEntries();
    bool streamOpen();

    // Real footer parsing (preferred path) — see CdiEngine.cpp for the
    // byte-level format (reverse-engineered DiscJuggler CDI structure).
    bool parseFooter();
    bool tryMountFilesystems();
    std::vector<uint8_t> readTrackRange(const CdiTrackInfo& t, uint64_t pos, uint64_t len) const;

    // Direct CDI→ISO sector reading (used by the legacy whole-file fallback
    // and raw single-track extraction)
    bool openCdiFile();
    void closeCdiFile();
    bool readStrippedSector(uint8_t* out2048);

    std::string m_path;
    bool m_isOpen = false;
    Type m_type = Type::Normal;
    uint32_t m_sectorSize = 2048;
    uint32_t m_seekHeader = 0;
    uint32_t m_seekEcc = 0;
    uint64_t m_totalSectors = 0;

    bool m_fallbackRaw = false;
    FILE* m_cdiFile = nullptr;

    struct archive* m_archive = nullptr;
    CdiIsoStream m_stream;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};

    // Real per-track layout from the footer, and any ISO 9660 filesystems
    // mounted from its data tracks (mirrors ChdEngine's multi-filesystem
    // handling: a disc can have more than one, e.g. GD-ROM's low-density +
    // high-density areas).
    std::vector<CdiTrackInfo> m_tracks;
    bool m_hasRealTracks = false; // true once parseFooter() has succeeded
    bool m_hasFilesystem = false; // true if at least one ISO 9660 mount succeeded
    std::vector<Iso9660Reader> m_isoMounts;
    std::vector<std::string> m_isoMountPrefixes;
    std::vector<int> m_isoMountTrackIdx; // parallel to m_isoMounts, index into m_tracks
};

#endif
