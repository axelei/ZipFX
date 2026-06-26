#include "AtrEngine.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ATR header layout (16 bytes):
//   [0-1]  Magic: 0x96 0x02
//   [2-3]  Paragraph count low word (LE)
//   [4-5]  Bytes per sector (LE): 128 = SD, 256 = DD
//   [6-7]  Paragraph count high word (LE)
//   [8-15] Unused
//
// Sector addressing (1-indexed):
//   Sectors 1-3:  always 128 bytes (boot sectors)
//   Sectors 4+:   m_bps bytes each
//   Offset = 16 + (N-1)*128           for N <= 3
//   Offset = 16 + 3*128 + (N-4)*m_bps for N > 3

static constexpr size_t kAtrHeaderSize = 16;
static constexpr size_t kBootSecSize   = 128;

AtrEngine::~AtrEngine()
{
    AtrEngine::Close();
}

uint64_t AtrEngine::sectorOffset(uint32_t n) const
{
    if (n == 0) return static_cast<uint64_t>(-1);
    if (n <= 3)
        return kAtrHeaderSize + static_cast<uint64_t>(n - 1) * kBootSecSize;
    return kAtrHeaderSize + 3 * kBootSecSize +
           static_cast<uint64_t>(n - 4) * m_bps;
}

const uint8_t* AtrEngine::sectorPtr(uint32_t n) const
{
    uint64_t off  = sectorOffset(n);
    uint64_t size = (n <= 3) ? kBootSecSize : m_bps;
    if (off == static_cast<uint64_t>(-1)) return nullptr;
    if (off + size > m_diskData.size())   return nullptr;
    return m_diskData.data() + off;
}

std::vector<uint8_t> AtrEngine::readSector(uint32_t n) const
{
    const uint8_t* p    = sectorPtr(n);
    uint64_t       size = (n <= 3) ? kBootSecSize : m_bps;
    if (!p) return {};
    return std::vector<uint8_t>(p, p + size);
}

// Follow the Atari DOS 2.x sector chain from firstSector.
// Each sector's last 3 bytes:
//   [bps-3]: (file_num << 2) | (next_sector bits 9:8)
//   [bps-2]: next_sector bits 7:0
//   [bps-1]: bytes_used in this sector
// Chain ends when next_sector == 0.
std::vector<uint8_t> AtrEngine::readChain(uint16_t firstSector) const
{
    std::vector<uint8_t> result;
    result.reserve(4096);

    uint16_t cur  = firstSector;
    int      guard = 4096; // safety limit

    while (cur != 0 && guard-- > 0) {
        if (m_extractCancelled) break;

        uint32_t secSize = (cur <= 3) ? static_cast<uint32_t>(kBootSecSize)
                                      : static_cast<uint32_t>(m_bps);
        const uint8_t* p = sectorPtr(cur);
        if (!p) break;

        // Sector link is in the last 3 bytes
        uint16_t nextSec   = (static_cast<uint16_t>(p[secSize - 3] & 0x03) << 8) |
                              p[secSize - 2];
        uint8_t  bytesUsed = p[secSize - 1];

        uint32_t dataBytes = (bytesUsed <= secSize - 3)
                             ? bytesUsed
                             : static_cast<uint32_t>(secSize - 3);

        result.insert(result.end(), p, p + dataBytes);
        cur = nextSec;
    }

    return result;
}

bool AtrEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) { LOG_ERR("ATR: failed to open %s", m_path.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    if (sz < static_cast<long>(kAtrHeaderSize + kBootSecSize)) {
        std::fclose(f);
        return false;
    }

    m_diskData.resize(static_cast<size_t>(sz));
    size_t n = std::fread(m_diskData.data(), 1, m_diskData.size(), f);
    std::fclose(f);
    m_diskData.resize(n);

    // Validate magic
    if (m_diskData[0] != 0x96 || m_diskData[1] != 0x02) {
        LOG_ERR("ATR: bad magic in %s", m_path.c_str());
        return false;
    }

    // Bytes per sector
    m_bps = static_cast<uint16_t>(m_diskData[4]) |
            (static_cast<uint16_t>(m_diskData[5]) << 8);
    if (m_bps != 128 && m_bps != 256) {
        LOG_WARN("ATR: unusual bps=%u in %s, defaulting to 128", m_bps, m_path.c_str());
        m_bps = 128;
    }

    // Read directory sectors 361-368 (each sector = m_bps bytes, 16-byte entries)
    const int kDirStart   = 361;
    const int kDirEnd     = 368;
    const int kEntrySize  = 16;
    const int entriesPerSec = static_cast<int>(m_bps) / kEntrySize;

    for (int secNum = kDirStart; secNum <= kDirEnd; secNum++) {
        const uint8_t* sec = sectorPtr(static_cast<uint32_t>(secNum));
        if (!sec) break;

        for (int e = 0; e < entriesPerSec; e++) {
            const uint8_t* entry = sec + e * kEntrySize;

            uint8_t flags = entry[0];

            // In-use bit: bit 6 (0x40)
            if ((flags & 0x40) == 0) continue;

            // Sector count and first sector
            uint16_t sectorCount  = static_cast<uint16_t>(entry[1]) |
                                    (static_cast<uint16_t>(entry[2]) << 8);
            uint16_t firstSector  = static_cast<uint16_t>(entry[3]) |
                                    (static_cast<uint16_t>(entry[4]) << 8);

            if (firstSector == 0) continue;

            // Filename: 8 chars + 3-char extension (space-padded)
            char fname[9] = {}, fext[4] = {};
            std::memcpy(fname, entry + 5, 8);
            std::memcpy(fext,  entry + 13, 3);

            // Strip trailing spaces
            auto stripSpaces = [](char* buf, int len) {
                for (int i = len - 1; i >= 0 && (buf[i] == ' ' || buf[i] == '\0'); i--)
                    buf[i] = '\0';
            };
            stripSpaces(fname, 8);
            stripSpaces(fext, 3);

            if (fname[0] == '\0') continue;

            std::string name(fname);
            if (fext[0] != '\0') { name += '.'; name += fext; }

            uint32_t approxSize = sectorCount * (static_cast<uint32_t>(m_bps) - 3);

            AtrEntry ae;
            ae.name        = name;
            ae.firstSector = firstSector;
            ae.sectorCount = sectorCount;
            ae.size        = approxSize;
            m_atrEntries.push_back(ae);

            ArchiveEntry archEntry;
            archEntry.name              = name;
            archEntry.path              = name;
            archEntry.size              = approxSize;
            archEntry.packedSize        = approxSize;
            archEntry.isDirectory       = false;
            archEntry.compressionMethod = "Uncompressed";
            m_entries.push_back(std::move(archEntry));
        }
    }

    m_isOpen = true;
    LOG_DBG("ATR: opened %s (bps=%u, %d files)",
            m_path.c_str(), m_bps, static_cast<int>(m_entries.size()));
    return true;
}

void AtrEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_atrEntries.clear();
    m_diskData.clear();
    m_path.clear();
    m_bps = 128;
}

const std::vector<ArchiveEntry>& AtrEngine::ListContents()
{
    return m_entries;
}

bool AtrEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    auto data = ReadFile(entryName);
    if (data.empty()) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool AtrEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    bool allOk = true;
    for (const auto& entry : m_entries) {
        if (m_extractCancelled) break;
        if (entry.isDirectory) continue;
        std::string outPath = (fs::path(destPath) / entry.path).string();
        if (!Extract(entry.path, outPath))
            allOk = false;
    }
    return allOk;
}

std::vector<uint8_t> AtrEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    std::string key(entryName);
    const AtrEntry* found = nullptr;
    for (const auto& ae : m_atrEntries) {
        if (ae.name == key) { found = &ae; break; }
    }
    if (!found) return {};
    if (found->firstSector == 0) return {};

    return readChain(found->firstSector);
}

std::vector<uint8_t> AtrEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes) data.resize(maxBytes);
    return data;
}

bool AtrEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()>         cancelFlag)
{
    if (!m_isOpen) return false;
    if (cancelFlag && cancelFlag()) return false;
    if (progressCallback) progressCallback(0, 1);

    int total = static_cast<int>(m_atrEntries.size());
    int i     = 0;
    for (const auto& ae : m_atrEntries) {
        if (cancelFlag && cancelFlag()) return false;
        if (sectorPtr(ae.firstSector) == nullptr) return false;
        if (progressCallback) progressCallback(++i, total);
    }
    return true;
}
