#ifndef ZIPFX_FAT_ENGINE_H
#define ZIPFX_FAT_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Read-only engine for FAT12 floppy disk images (.st, .img, .ima, .vfd).
// Parses the BIOS Parameter Block, FAT12 cluster chain, root directory, and
// subdirectories.  Files are presented with their full relative paths.
class FatEngine : public ArchiveEngine
{
public:
    ~FatEngine() override;

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

    std::string_view FormatName() const override { return "FAT12 Floppy"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    // Look up FAT12 cluster value for cluster N.
    uint16_t fatEntry(uint16_t cluster) const;
    // Collect all data bytes for a cluster chain starting at firstCluster.
    std::vector<uint8_t> readClusterChain(uint16_t firstCluster, uint32_t fileSize) const;
    // Return byte offset of the first byte of cluster N in m_diskData.
    uint64_t clusterOffset(uint16_t cluster) const;
    // Scan a directory (either root or sub).  rootData = null means use root dir.
    void scanDirectory(const uint8_t* dirData, size_t dirSize,
                       const std::string& pathPrefix, int depth = 0);

    struct FatEntry {
        std::string name;         // archive-relative path
        uint16_t    firstCluster;
        uint32_t    fileSize;
    };

    std::vector<FatEntry>     m_fatEntries;
    std::vector<uint8_t>      m_diskData;
    std::vector<ArchiveEntry> m_entries;
    std::string               m_path;

    // BPB fields
    uint16_t m_bps            = 512; // bytes per sector
    uint8_t  m_spc            = 1;   // sectors per cluster
    uint32_t m_fatStart       = 0;   // byte offset of FAT1
    uint32_t m_rootDirStart   = 0;   // byte offset of root directory
    uint32_t m_dataStart      = 0;   // byte offset of cluster 2
    uint16_t m_rootDirEntries = 0;   // max root directory entries
    uint32_t m_fatSize        = 0;   // FAT size in bytes

    bool              m_isOpen = false;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
