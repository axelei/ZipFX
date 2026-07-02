#ifndef ZIPFX_CHD_ENGINE_H
#define ZIPFX_CHD_ENGINE_H

#include "ArchiveEngine.h"
#include "Iso9660Reader.h"

#include <fstream>

typedef struct _chd_file chd_file;

class ChdEngine : public ArchiveEngine
{
public:
    ~ChdEngine() override;

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

    std::string_view FormatName() const override { return m_formatLabel; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_chd != nullptr; }

private:
    struct TrackInfo {
        std::string name;
        std::string type;
        int number = 0;
        uint32_t frames = 0;
        uint64_t size = 0;          // extracted/cooked byte size for this track

        // CD-ROM tracks (isCdTrack=true) are physically stored as fixed
        // 2448-byte frames (2352-byte sector + 96-byte subcode), regardless
        // of the declared TYPE. startFrame/headerOff describe where the
        // "cooked" user-data region begins within that frame layout.
        bool isCdTrack = false;
        uint64_t startFrame = 0;    // first physical CD frame of this track
        uint32_t headerOff = 0;     // offset of user data within the 2352-byte sector
        uint32_t userSize = 0;      // bytes of user data per frame (== size/frames)

        uint64_t offset = 0;        // byte offset in the logical image (non-CD tracks only)
    };

    void parseMetadata();
    std::vector<uint8_t> readRange(uint64_t offset, uint64_t length);
    std::vector<uint8_t> readTrackData(const TrackInfo& t, uint64_t trackPos, uint64_t length);
    bool extractTrackData(const TrackInfo& t, std::ofstream& out);
    bool tryMountFilesystem();
    void buildCueSheet();

    chd_file* m_chd = nullptr;
    std::string m_path;
    std::string m_formatLabel = "CHD";
    uint32_t m_hunkBytes = 0;
    uint64_t m_logicalBytes = 0;

    // Single-hunk decode cache — CD-track reads touch the same hunk
    // repeatedly (8 frames/hunk), so avoid re-decompressing it each time.
    uint32_t m_cachedHunk = UINT32_MAX;
    std::vector<uint8_t> m_hunkCache;
    std::vector<TrackInfo> m_tracks;

    // GD-ROM (Dreamcast) discs master their high-density-area ISO 9660
    // filesystem with disc-absolute LBAs (conventionally based at 45000),
    // unlike ordinary CD-ROM tracks whose filesystem LBAs are relative to
    // that track's own start.
    bool m_isGdRom = false;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};

    // ISO 9660 filesystems mounted from data tracks, when present. Discs can
    // have more than one (GD-ROM discs have a small "low density" system
    // area plus the large "high density" game data area) — each is kept as
    // its own Iso9660Reader, and ReadFile/Extract check them in turn.
    bool m_hasFilesystem = false;
    std::vector<Iso9660Reader> m_isoMounts;
    std::vector<std::string> m_isoMountPrefixes; // "" or "Track NN/", parallel to m_isoMounts

    // Synthesized .cue sheet describing the track layout, exposed as an
    // extra entry when no filesystem could be mounted (raw track fallback).
    std::string m_cueSheetName;
    std::string m_cueSheetText;
};

#endif
