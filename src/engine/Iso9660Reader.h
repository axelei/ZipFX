#ifndef ZIPFX_ISO9660_READER_H
#define ZIPFX_ISO9660_READER_H

#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// Self-contained ISO 9660 filesystem reader.
// The caller supplies a SectorFn that reads exactly 2048 bytes of user data
// for any logical block address. The reader handles both standard ISO 9660
// (8.3 filenames) and Joliet extensions (UCS-2 Unicode names).
class Iso9660Reader
{
public:
    // Returns true on success. `out2048` receives exactly 2048 bytes.
    using SectorFn = std::function<bool(uint32_t lba, uint8_t* out2048)>;

    struct Entry
    {
        std::string path;       // archive-relative path with '/' separators
        uint32_t    lba   = 0;  // starting logical block
        uint32_t    size  = 0;  // file size in bytes (0 for directories)
        bool        isDir = false;
        time_t      mtime = 0;
    };

    // Parse the filesystem. Returns false if no valid ISO 9660 VD is found.
    // vdStartLba: the LBA at which to start scanning for Volume Descriptors
    // (default 16 for standalone ISO images; pass trackStartLba+16 for GDI/CDs
    // that use disc-absolute LBAs inside the ISO 9660 structures).
    bool open(SectorFn reader, uint32_t vdStartLba = 16);

    bool isOpen() const { return !m_entries.empty() || m_opened; }
    const std::vector<Entry>& entries() const { return m_entries; }

    // Stream file data: calls consumer(ptr, len) for each sector chunk until
    // `size` bytes are delivered. Stops early if consumer returns false.
    bool readData(uint32_t lba, uint32_t size,
                  const std::function<bool(const uint8_t*, size_t)>& consumer) const;

private:
    bool parseVolumeDescriptors();
    void walkDir(uint32_t lba, uint32_t size, const std::string& prefix, bool joliet,
                 std::unordered_set<uint32_t>& visitedLbas);
    static time_t decodeDate(const uint8_t* d7);

    SectorFn           m_reader;
    std::vector<Entry> m_entries;
    bool               m_opened   = false;
    uint32_t           m_vdStart  = 16;
};

// Detect whether a raw disc image uses 2352-byte sectors (with sync header) or
// 2048-byte cooked sectors. `buf` must be at least 24 bytes from the image start.
// Sets sectorSize and headerOff on return.
inline void detectRawSectorFormat(const uint8_t* buf24,
                                   uint32_t& sectorSize, uint32_t& headerOff)
{
    static const uint8_t kSync[12] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
    };
    if (std::memcmp(buf24, kSync, 12) == 0)
    {
        sectorSize = 2352;
        // Byte 15 = mode: 1 → Mode 1 (header 12+4=16 bytes),
        //                  2 → Mode 2 Form 1 (add 8-byte subheader → 24 bytes)
        headerOff = (buf24[15] == 2) ? 24u : 16u;
    }
    else
    {
        sectorSize = 2048;
        headerOff  = 0;
    }
}

#endif
