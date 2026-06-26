#ifndef ZIPFX_DSK_ENGINE_H
#define ZIPFX_DSK_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class DskEngine : public ArchiveEngine
{
public:
    ~DskEngine() override;

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
    void loadDiskData();

    // Apple DOS / ProDOS filesystem parsing
    bool tryParseApple();
    bool parseApple2ImgHeader();
    bool parseDos33();
    bool parseProDos();

    std::vector<uint8_t> readDos33File(uint8_t firstTrack, uint8_t firstSector,
                                        uint32_t totalSize) const;
    std::vector<uint8_t> readProDosFile(uint16_t keyBlock, uint8_t storageType,
                                         uint32_t totalSize) const;

    struct AppleEntry {
        std::string name;
        uint32_t size = 0;
        uint8_t fileType = 0;
        bool isDirectory = false;
        uint8_t firstTrack = 0;
        uint8_t firstSector = 0;
        uint16_t keyBlock = 0;
        uint8_t storageType = 0;
    };

    std::vector<AppleEntry> m_appleEntries;
    std::vector<uint8_t> m_diskData;
    bool m_isAppleDisk = false;
    int m_appleTracks = 0;

    std::string m_path;
    bool m_isOpen = false;
    std::string m_formatName = "DSK";
    std::string m_formatDesc;
    uint64_t m_dataSize = 0;
    uint64_t m_dataOffset = 0;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
