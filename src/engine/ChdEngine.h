#ifndef ZIPFX_CHD_ENGINE_H
#define ZIPFX_CHD_ENGINE_H

#include "ArchiveEngine.h"

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
        uint32_t frames = 0;
        uint32_t sectorSize = 0;
        uint64_t offset = 0;    // byte offset in the logical image
        uint64_t size = 0;      // total bytes for this track
    };

    void parseMetadata();
    std::vector<uint8_t> readRange(uint64_t offset, uint64_t length);

    chd_file* m_chd = nullptr;
    std::string m_path;
    std::string m_formatLabel = "CHD";
    uint32_t m_hunkBytes = 0;
    uint64_t m_logicalBytes = 0;
    std::vector<TrackInfo> m_tracks;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
