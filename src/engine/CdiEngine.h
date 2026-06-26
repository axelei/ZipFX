#ifndef ZIPFX_CDI_ENGINE_H
#define ZIPFX_CDI_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <vector>

struct archive;
struct archive_entry;

// Stream state passed to libarchive callbacks
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

    bool detectType();
    bool ensureArchiveOpen();
    bool loadAllEntries();
    bool streamOpen();

    // Direct CDI→ISO sector reading (used in fallback and raw extraction)
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
};

#endif
