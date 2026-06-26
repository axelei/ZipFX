#ifndef ZIPFX_GDI_ENGINE_H
#define ZIPFX_GDI_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct GdiTrack {
    int number = 0;
    int lba = 0;
    int sectorType = 0;
    int sectorSize = 2352;
    std::string fileName;
    int extOffset = 0;
    std::string resolvedPath;
};

class GdiEngine : public ArchiveEngine
{
public:
    ~GdiEngine() override;

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

    std::string_view FormatName() const override { return "GDI"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    bool parseGdi();

    std::string m_path;
    std::string m_baseDir;
    bool m_isOpen = false;
    std::vector<GdiTrack> m_tracks;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
