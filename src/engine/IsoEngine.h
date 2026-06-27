#ifndef ZIPFX_ISO_ENGINE_H
#define ZIPFX_ISO_ENGINE_H

#include "ArchiveEngine.h"
#include "Iso9660Reader.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

class IsoEngine : public ArchiveEngine
{
public:
    ~IsoEngine() override;

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

    std::string_view FormatName() const override { return "ISO"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    const Iso9660Reader::Entry* findEntry(std::string_view path) const;
    bool buildSectorReader();

    std::string  m_path;
    bool         m_isOpen     = false;
    FILE*        m_file       = nullptr;
    uint32_t     m_sectorSize = 2048;
    uint32_t     m_headerOff  = 0;

    Iso9660Reader          m_iso;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool>      m_extractCancelled{false};
};

#endif
