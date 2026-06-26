#include "DskEngine.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── Apple disk geometry ───────────────────────────────────────────────────
static constexpr int kAppleSecPerTrack = 16;
static constexpr int kAppleSectorSize = 256;
static constexpr int kAppleTrackSize = kAppleSecPerTrack * kAppleSectorSize;
static constexpr int kDos33Tracks = 35;

// ── ProDOS constants ─────────────────────────────────────────────────────
static constexpr int kProDOSBlockSize = 512;
static constexpr int kProDOSEntrySize = 39;
static constexpr int kProDOSVolDirBlock = 2;

static constexpr uint8_t kProDosSeedling  = 0x10;
static constexpr uint8_t kProDosSapling   = 0x20;
static constexpr uint8_t kProDosTree      = 0x30;
static constexpr uint8_t kProDosSubdir    = 0xD0;
static constexpr uint8_t kProDosVolumeDir = 0xF0;

// ── Format detection ─────────────────────────────────────────────────────

enum class DskFormat { Unknown, Raw, D64, D71, D80, D82, CpcDsk, Apple2Img, AppleDc42 };

struct DskProbe {
    DskFormat fmt;
    const char* name;
    const char* desc;
    bool (*match)(const uint8_t* hdr, size_t len, long fileSize);
};

static const DskProbe kProbes[] = {
    { DskFormat::CpcDsk, "DSK (CPC)", "Amstrad CPC disk image",
        [](const uint8_t* d, size_t len, long) -> bool {
            if (len < 8) return false;
            return (std::memcmp(d, "MV - CPC", 8) == 0) ||
                   (std::memcmp(d, "EXTENDED", 8) == 0 &&
                    len >= 16 && std::memcmp(d + 8, " CPC", 4) == 0);
        }},
    { DskFormat::Apple2Img, "DSK (2IMG)", "Apple II 2IMG disk image",
        [](const uint8_t* d, size_t len, long) -> bool {
            return len >= 4 && std::memcmp(d, "2IMG", 4) == 0;
        }},
    { DskFormat::AppleDc42, "DC42", "Apple Disk Copy 4.2",
        [](const uint8_t* d, size_t len, long) -> bool {
            return len >= 3 && d[0] == 0x01 && d[1] == 0x00 && d[2] == 'D';
        }},
    { DskFormat::D64, "D64", "Commodore 64 disk image",
        [](const uint8_t* d, size_t len, long fileSize) -> bool {
            if (fileSize != 174848 && fileSize != 175531 &&
                fileSize != 196608 && fileSize != 197376 &&
                fileSize != 201600 && fileSize != 205800)
            {
                if (len < 6) return false;
                return d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x00 &&
                       d[3] == 0x00 && d[4] == 0x00 && d[5] != 0x00;
            }
            return true;
        }},
};

DskEngine::~DskEngine()
{
    DskEngine::Close();
}

bool DskEngine::detectFormat()
{
    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long fileSize = std::ftell(f);

    uint8_t header[64];
    std::rewind(f);
    size_t n = std::fread(header, 1, sizeof(header), f);
    std::fclose(f);

    for (const auto& p : kProbes)
    {
        if (p.match(header, n, fileSize))
        {
            m_formatName = p.name;
            m_formatDesc = p.desc;
            m_dataOffset = 0;
            m_dataSize = static_cast<uint64_t>(fileSize);
            return true;
        }
    }

    m_formatName = "Raw Disk";
    m_formatDesc = "Unrecognized disk image";
    m_dataOffset = 0;
    m_dataSize = static_cast<uint64_t>(fileSize);
    return fileSize > 0;
}

void DskEngine::loadDiskData()
{
    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return;
    m_diskData.resize(m_dataSize);
    std::fseek(f, static_cast<long>(m_dataOffset), SEEK_SET);
    size_t n = std::fread(m_diskData.data(), 1, m_dataSize, f);
    std::fclose(f);
    if (n != m_dataSize)
        m_diskData.resize(n);
}

bool DskEngine::readRaw(FILE* f, std::vector<uint8_t>& out)
{
    out.resize(m_dataSize);
    std::fseek(f, static_cast<long>(m_dataOffset), SEEK_SET);
    size_t n = std::fread(out.data(), 1, m_dataSize, f);
    if (n != m_dataSize)
        out.resize(n);
    return !out.empty();
}

// ── Raw sector access (physical order — no skew) ─────────────────────────
// In a .dsk file, sectors are stored sequentially: T0S0, T0S1, ..., T0S15,
// T1S0, ... Apple DOS uses logical sector numbers internally, but in the
// .dsk image file the sectors are arranged in physical order.
static uint64_t physSectorOffset(int track, int sector)
{
    return static_cast<uint64_t>(track * kAppleSecPerTrack + sector) * kAppleSectorSize;
}

static const uint8_t* physSectorPtr(const std::vector<uint8_t>& data, int track, int sector)
{
    uint64_t off = physSectorOffset(track, sector);
    if (off + kAppleSectorSize > data.size())
        return nullptr;
    return data.data() + off;
}

// ── Apple 2IMG header parser ─────────────────────────────────────────────

bool DskEngine::parseApple2ImgHeader()
{
    if (m_diskData.size() < 64) return false;
    if (std::memcmp(m_diskData.data(), "2IMG", 4) != 0) return false;

    uint32_t hdrLen = static_cast<uint32_t>(m_diskData[28]) |
                      (static_cast<uint32_t>(m_diskData[29]) << 8) |
                      (static_cast<uint32_t>(m_diskData[30]) << 16) |
                      (static_cast<uint32_t>(m_diskData[31]) << 24);
    if (hdrLen == 0) hdrLen = 64;

    m_dataOffset = static_cast<uint32_t>(m_diskData[12]) |
                   (static_cast<uint32_t>(m_diskData[13]) << 8) |
                   (static_cast<uint32_t>(m_diskData[14]) << 16) |
                   (static_cast<uint32_t>(m_diskData[15]) << 24);

    uint16_t fmt = static_cast<uint16_t>(m_diskData[6]) |
                   (static_cast<uint16_t>(m_diskData[7]) << 8);

    uint16_t numBlocks = static_cast<uint16_t>(m_diskData[10]) |
                         (static_cast<uint16_t>(m_diskData[11]) << 8);

    m_dataSize = static_cast<uint64_t>(numBlocks) * 512;
    if (m_dataSize == 0)
        m_dataSize = m_diskData.size() - m_dataOffset;

    if (m_dataOffset > m_diskData.size() || m_dataOffset + m_dataSize > m_diskData.size())
        return false;

    switch (fmt)
    {
    case 1: m_formatName = "DSK (2IMG DOS 3.3)"; m_formatDesc = "Apple II DOS 3.3 disk image"; break;
    case 2: m_formatName = "DSK (2IMG ProDOS)";  m_formatDesc = "Apple II ProDOS disk image";  break;
    default: m_formatName = "DSK (2IMG)";         m_formatDesc = "Apple II disk image";          break;
    }

    if (m_dataOffset > 0) {
        std::vector<uint8_t> raw(m_diskData.begin() + static_cast<ptrdiff_t>(m_dataOffset),
                                 m_diskData.begin() + static_cast<ptrdiff_t>(m_dataOffset + m_dataSize));
        m_diskData = std::move(raw);
        m_dataOffset = 0;
    }
    return true;
}

// ── DOS 3.3 logical-to-physical sector skew ──────────────────────────────
// DOS 3.3 uses a skew table (RWTS $B7E9) for rotational optimization.
// In a .dsk file sectors are stored in physical order, so to find the
// physical sector that contains DOS logical sector L, use kDos33LogToPhys[L].
static const uint8_t kDos33LogToPhys[16] = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15
};

// Return file offset of a DOS logical sector (track, logicalSector)
// accounting for the skew table.
static uint64_t logicalSectorOffset(int track, int logicalSector)
{
    int physSector = kDos33LogToPhys[logicalSector & 0x0F];
    return static_cast<uint64_t>(track * kAppleSecPerTrack + physSector) * kAppleSectorSize;
}

// Return pointer to the data of a DOS logical sector
static const uint8_t* logicalSectorPtr(const std::vector<uint8_t>& data,
                                        int track, int logicalSector)
{
    uint64_t off = logicalSectorOffset(track, logicalSector);
    if (off + kAppleSectorSize > data.size())
        return nullptr;
    return data.data() + off;
}

// ── Apple DOS 3.3 parser ─────────────────────────────────────────────────
// The VTOC and catalog are on track 17. The .dsk file stores all sectors
// (including track 17) in physical order. The VTOC's catalog pointer
// and catalog-chain pointers use LOGICAL sector numbers. Since logical 0
// maps to physical 0 (kDos33LogToPhys[0]=0), the VTOC always appears at
// the same physical position regardless of ordering convention. For other
// logical sectors on track 17 we need the skew translation.
//
// File-data sectors referenced in catalog entries also use logical sector
// numbers, so we translate them through the skew table when reading.

bool DskEngine::parseDos33()
{
    // VTOC at logical track 17, logical sector 0 (= physical sector 0)
    const uint8_t* vtoc = physSectorPtr(m_diskData, 17, 0);
    if (!vtoc) return false;

    // VTOC signature: byte 1 should be 0x11 (track 17)
    if (vtoc[1] != 0x11) return false;

    // Get catalog location from VTOC bytes $00 (sector) and $01 (track)
    int catLogSec = vtoc[0];
    int catTrack = vtoc[1];

    // Scan ALL physical sectors on track 17 for valid catalog entries.
    // We do this because many real-world Apple II disk images have the
    // VTOC catalog pointer pointing to logical sector 4, but the actual
    // entries may be at different physical positions (various copying
    // tools and disk utilities don't always maintain the catalog chain).
    //
    // We also need to handle the catalog-chain links: each catalog
    // sector's bytes $00-$01 form a (sector, track) link to the next
    // catalog sector. We follow the chain from the VTOC-designated start
    // sector, but also scan all other sectors on track 17 for entries
    // that might not be properly linked.

    // Track which logical sectors we've already processed
    bool visited[16] = {false};

    // First, try following the chain from the VTOC pointer
    int t = catTrack;
    int s = catLogSec;
    int maxChain = 50;

    while (maxChain-- > 0)
    {
        if (t != 17 || s < 0 || s >= 16) break;
        if (visited[s]) break;
        visited[s] = true;

        const uint8_t* sec = logicalSectorPtr(m_diskData, t, s);
        if (!sec) break;

        int nextS = sec[0];
        int nextT = sec[1];

        if (nextT == 0xFF || nextS == 0xFF || (nextT == 0 && nextS == 0))
            nextT = nextS = 0xFF;

        // Parse file entries (7 per sector, 35 bytes each, starting at $0B)
        for (int i = 0; i < 7; i++)
        {
            int entryOff = 0x0B + i * 35;
            if (entryOff + 35 > kAppleSectorSize) break;

            const uint8_t* e = sec + entryOff;

            if (e[0] == 0xFF || e[0] == 0x00) continue;

            char nameBuf[31];
            int nameLen = 0;
            for (int c = 0; c < 30; c++) {
                char ch = static_cast<char>(e[3 + c] & 0x7F);
                if (ch == ' ' || ch == '\0') break;
                nameBuf[nameLen++] = ch;
            }
            if (nameLen == 0) continue;
            nameBuf[nameLen] = '\0';

            uint8_t typeByte = e[2];
            uint16_t numSectors = static_cast<uint16_t>(e[0x21]) |
                                  (static_cast<uint16_t>(e[0x22]) << 8);

            int ft = e[0];
            int fs = e[1];
            if (ft == 17 || ft > 34) continue;
            if (fs < 0 || fs >= 16) continue;
            if (numSectors == 0 || numSectors > 560) continue;

            uint32_t fileSize = static_cast<uint32_t>(numSectors) * kAppleSectorSize;

            AppleEntry ae;
            ae.name = nameBuf;
            ae.size = fileSize;
            ae.fileType = typeByte;
            ae.isDirectory = false;
            ae.firstTrack = static_cast<uint8_t>(ft);
            ae.firstSector = static_cast<uint8_t>(fs);

            m_appleEntries.push_back(ae);
        }

        if (nextT == 0xFF || nextS == 0xFF) break;
        if (nextT > 35 || nextS >= 16) break;
        t = nextT;
        s = nextS;

        if (visited[s]) break;
    }

    for (int ps = 0; ps < 16; ps++)
    {
        int logS = -1;
        for (int i = 0; i < 16; i++) {
            if (kDos33LogToPhys[i] == ps) { logS = i; break; }
        }
        if (logS < 0 || logS >= 16) continue;
        if (visited[logS]) continue;

        const uint8_t* sec = physSectorPtr(m_diskData, 17, ps);
        if (!sec) continue;

        bool hasEntries = false;
        for (int i = 0; i < 7; i++) {
            const uint8_t* e = sec + 0x0B + i * 35;
            if (e[0] != 0xFF && e[0] != 0x00) {
                for (int c = 0; c < 30; c++) {
                    if ((e[3 + c] & 0x7F) != ' ' && e[3 + c] != '\0') {
                        hasEntries = true;
                        break;
                    }
                }
            }
            if (hasEntries) break;
        }
        if (!hasEntries) continue;

        visited[logS] = true;

        for (int i = 0; i < 7; i++)
        {
            int entryOff = 0x0B + i * 35;
            if (entryOff + 35 > kAppleSectorSize) break;

            const uint8_t* e = sec + entryOff;

            if (e[0] == 0xFF || e[0] == 0x00) continue;

            char nameBuf[31];
            int nameLen = 0;
            for (int c = 0; c < 30; c++) {
                char ch = static_cast<char>(e[3 + c] & 0x7F);
                if (ch == ' ' || ch == '\0') break;
                nameBuf[nameLen++] = ch;
            }
            if (nameLen == 0) continue;
            nameBuf[nameLen] = '\0';

            uint8_t typeByte = e[2];
            uint16_t numSectors = static_cast<uint16_t>(e[0x21]) |
                                  (static_cast<uint16_t>(e[0x22]) << 8);

            int ft = e[0];
            int fs = e[1];
            if (ft == 17 || ft > 34) continue;
            if (fs < 0 || fs >= 16) continue;
            if (numSectors == 0 || numSectors > 560) continue;

            uint32_t fileSize = static_cast<uint32_t>(numSectors) * kAppleSectorSize;

            AppleEntry ae;
            ae.name = nameBuf;
            ae.size = fileSize;
            ae.fileType = typeByte;
            ae.isDirectory = false;
            ae.firstTrack = static_cast<uint8_t>(ft);
            ae.firstSector = static_cast<uint8_t>(fs);

            m_appleEntries.push_back(ae);
        }
    }

    if (m_appleEntries.empty()) return false;
    m_formatDesc = "Apple DOS 3.3 (" + std::to_string(m_appleEntries.size()) + " files)";
    return true;
}

// ── Apple ProDOS parser ──────────────────────────────────────────────────

bool DskEngine::parseProDos()
{
    int numBlocks = static_cast<int>(m_diskData.size()) / kProDOSBlockSize;
    if (numBlocks < 3) return false;

    auto blockPtr = [&](int bn) -> const uint8_t* {
        int64_t off = static_cast<int64_t>(bn) * kProDOSBlockSize;
        if (off + kProDOSBlockSize > static_cast<int64_t>(m_diskData.size()))
            return nullptr;
        return m_diskData.data() + off;
    };

    const uint8_t* volBlock = blockPtr(kProDOSVolDirBlock);
    if (!volBlock) return false;

    uint8_t stNameLen = volBlock[0];
    uint8_t storageType = stNameLen & 0xF0;
    if (storageType != kProDosVolumeDir) return false;

    int nameLen = stNameLen & 0x0F;
    if (nameLen == 0) nameLen = 15;

    uint16_t entryLen = static_cast<uint16_t>(volBlock[23]) |
                        (static_cast<uint16_t>(volBlock[24]) << 8);
    if (entryLen == 0) entryLen = kProDOSEntrySize;

    uint16_t entriesPerBlock = static_cast<uint16_t>(volBlock[25]) |
                                (static_cast<uint16_t>(volBlock[26]) << 8);
    if (entriesPerBlock == 0) entriesPerBlock = 13;

    int fileCount = static_cast<int>(volBlock[27]) |
                    (static_cast<int>(volBlock[28]) << 8);
    if (fileCount <= 0) return false;

    int entryStart = 35;
    int entryOff = entryStart;
    int found = 0;

    while (entryOff + entryLen <= kProDOSBlockSize && found < fileCount)
    {
        const uint8_t* e = volBlock + entryOff;
        uint8_t se = e[0];
        if (se == 0x00) { entryOff += entryLen; continue; }

        uint8_t st = se & 0xF0;
        int fnameLen = se & 0x0F;
        if (fnameLen == 0 || fnameLen > 15) { entryOff += entryLen; continue; }

        if (st != kProDosSeedling && st != kProDosSapling && st != kProDosTree &&
            st != kProDosSubdir) { entryOff += entryLen; continue; }

        char nameBuf[16];
        for (int c = 0; c < fnameLen; c++)
            nameBuf[c] = static_cast<char>(e[1 + c]);
        nameBuf[fnameLen] = '\0';

        if (nameBuf[0] == '\0') { entryOff += entryLen; continue; }

        bool isDir = (st == kProDosSubdir);
        uint16_t fileType = static_cast<uint16_t>(e[16]) |
                            (static_cast<uint16_t>(e[17]) << 8);
        uint16_t keyBlock = static_cast<uint16_t>(e[18]) |
                            (static_cast<uint16_t>(e[19]) << 8);
        uint16_t numBlocks = static_cast<uint16_t>(e[20]) |
                             (static_cast<uint16_t>(e[21]) << 8);
        uint32_t fileSize = static_cast<uint32_t>(numBlocks) * kProDOSBlockSize;

        AppleEntry ae;
        ae.name = nameBuf;
        ae.size = fileSize;
        ae.fileType = static_cast<uint8_t>(fileType & 0xFF);
        ae.isDirectory = isDir;
        ae.keyBlock = keyBlock;
        ae.storageType = st;

        m_appleEntries.push_back(ae);
        found++;
        entryOff += entryLen;
    }

    if (found == 0) return false;
    m_formatDesc = "Apple ProDOS (" + std::to_string(found) + " files)";
    return true;
}

// ── Try to parse as Apple disk ───────────────────────────────────────────

bool DskEngine::tryParseApple()
{
    if (m_diskData.empty())
        return false;

    // Handle 2IMG wrapper
    if (m_diskData.size() >= 4 && std::memcmp(m_diskData.data(), "2IMG", 4) == 0)
    {
        if (!parseApple2ImgHeader())
            return false;
    }

    if (m_diskData.empty())
        return false;

    // Determine track count
    m_appleTracks = static_cast<int>(m_diskData.size()) / kAppleTrackSize;
    if (m_appleTracks < 18)
        return false;

    // Try DOS 3.3 first, then ProDOS
    bool ok = parseDos33();
    if (!ok)
        ok = parseProDos();

    if (!ok)
        return false;

    // Populate ArchiveEntry list from AppleEntry list
    m_isAppleDisk = true;
    for (const auto& ae : m_appleEntries)
    {
        ArchiveEntry entry;
        entry.name = ae.name;
        entry.path = ae.name;
        if (ae.isDirectory)
        {
            std::string dirName = ae.name + "/";
            entry.name = dirName;
            entry.path = dirName;
            entry.size = 0;
            entry.packedSize = 0;
            entry.isDirectory = true;
        }
        else
        {
            entry.size = ae.size;
            entry.packedSize = ae.size;
            entry.isDirectory = false;
            entry.compressionMethod = "Uncompressed";
        }
        m_entries.push_back(std::move(entry));
    }
    return true;
}

// ── DOS 3.3 file reader ──────────────────────────────────────────────────
// Catalog entries store the first file sector as (track, logicalSector).
// The T/S list sectors and data sectors all use logical sector numbers.
// We translate through the skew table to get the physical position in
// the .dsk file.

std::vector<uint8_t> DskEngine::readDos33File(
    uint8_t firstTrack, uint8_t firstSector, uint32_t totalSize) const
{
    if (m_diskData.empty() || totalSize == 0)
        return {};

    std::vector<uint8_t> result;
    result.reserve(totalSize);

    int t = firstTrack;
    int s = firstSector;

    while (result.size() < totalSize)
    {
        const uint8_t* sec = logicalSectorPtr(m_diskData, t, s);
        if (!sec) break;

        int nextS = sec[0];
        int nextT = sec[1];

        for (int pairIdx = 0; pairIdx < 127; pairIdx++)
        {
            int pairOff = 2 + pairIdx * 2;
            if (pairOff + 2 > kAppleSectorSize) break;

            int dataS = sec[pairOff];
            int dataT = sec[pairOff + 1];

            if ((dataS == 0 && dataT == 0) || (dataS == 0xFF && dataT == 0xFF))
                break;

            const uint8_t* dataSec = logicalSectorPtr(m_diskData, dataT, dataS);
            if (!dataSec) break;

            uint32_t remaining = totalSize - static_cast<uint32_t>(result.size());
            uint32_t toCopy = (remaining < kAppleSectorSize) ? remaining : kAppleSectorSize;
            result.insert(result.end(), dataSec, dataSec + toCopy);

            if (result.size() >= totalSize) break;
        }

        if (result.size() >= totalSize) break;

        t = nextT;
        s = nextS;

        if (t <= 0 || t >= m_appleTracks) break;
        if (s < 0 || s >= kAppleSecPerTrack) break;
    }

    return result;
}

// ── ProDOS file reader ──────────────────────────────────────────────────

std::vector<uint8_t> DskEngine::readProDosFile(
    uint16_t keyBlock, uint8_t storageType, uint32_t totalSize) const
{
    if (m_diskData.empty() || totalSize == 0)
        return {};

    int numBlocks = static_cast<int>(m_diskData.size()) / kProDOSBlockSize;

    auto readBlock = [&](int bn) -> std::vector<uint8_t> {
        int64_t off = static_cast<int64_t>(bn) * kProDOSBlockSize;
        if (off + kProDOSBlockSize > static_cast<int64_t>(m_diskData.size()))
            return {};
        return std::vector<uint8_t>(m_diskData.begin() + off,
                                    m_diskData.begin() + off + kProDOSBlockSize);
    };

    std::vector<uint8_t> result;
    result.reserve(totalSize);
    std::vector<int> dataBlocks;

    switch (storageType)
    {
    case kProDosSeedling:
        dataBlocks.push_back(keyBlock);
        break;

    case kProDosSapling:
    {
        auto indexBlock = readBlock(keyBlock);
        if (indexBlock.empty()) break;
        for (int i = 0; i < 256; i++)
        {
            uint16_t bn = static_cast<uint16_t>(indexBlock[i * 2]) |
                          (static_cast<uint16_t>(indexBlock[i * 2 + 1]) << 8);
            if (bn == 0 || bn >= numBlocks) break;
            dataBlocks.push_back(bn);
        }
        break;
    }

    case kProDosTree:
    {
        auto masterBlock = readBlock(keyBlock);
        if (masterBlock.empty()) break;
        for (int i = 0; i < 128; i++)
        {
            uint32_t idxBn = static_cast<uint32_t>(masterBlock[i * 4]) |
                             (static_cast<uint32_t>(masterBlock[i * 4 + 1]) << 8) |
                             (static_cast<uint32_t>(masterBlock[i * 4 + 2]) << 16) |
                             (static_cast<uint32_t>(masterBlock[i * 4 + 3]) << 24);
            if (idxBn == 0 || idxBn >= static_cast<uint32_t>(numBlocks)) break;

            auto indexBlock = readBlock(static_cast<int>(idxBn));
            if (indexBlock.empty()) break;
            for (int j = 0; j < 256; j++)
            {
                uint16_t bn = static_cast<uint16_t>(indexBlock[j * 2]) |
                              (static_cast<uint16_t>(indexBlock[j * 2 + 1]) << 8);
                if (bn == 0 || bn >= numBlocks) break;
                dataBlocks.push_back(bn);
            }
        }
        break;
    }
    default:
        break;
    }

    for (int bn : dataBlocks)
    {
        if (result.size() >= totalSize) break;
        auto block = readBlock(bn);
        if (block.empty()) break;
        uint32_t remaining = totalSize - static_cast<uint32_t>(result.size());
        uint32_t toCopy = (remaining < kProDOSBlockSize) ? remaining : kProDOSBlockSize;
        result.insert(result.end(), block.begin(), block.begin() + toCopy);
    }

    return result;
}

// ── Public API ──────────────────────────────────────────────────────────

bool DskEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    if (!detectFormat())
    {
        LOG_ERR("DSK: failed to open %s", m_path.c_str());
        return false;
    }

    if (m_dataSize == 0)
    {
        LOG_ERR("DSK: empty file %s", m_path.c_str());
        return false;
    }

    loadDiskData();
    if (m_diskData.empty())
        return false;

    // Try Apple filesystem parsing (DOS 3.3 or ProDOS)
    if (tryParseApple())
    {
        m_isOpen = true;
        if (m_formatName.find("2IMG") == std::string::npos)
            m_formatName = "DSK (Apple II)";
        LOG_DBG("DSK: opened %s as Apple II (%s, %d entries)",
                m_path.c_str(), m_formatDesc.c_str(), (int)m_entries.size());
        return true;
    }

    // Fallback: expose as a single .img file
    std::string stem = fs::path(m_path).stem().string();
    if (stem.empty()) stem = "disk";

    ArchiveEntry ae;
    ae.name = stem + ".img";
    ae.path = ae.name;
    ae.size = m_dataSize;
    ae.packedSize = m_dataSize;
    ae.isDirectory = false;
    ae.compressionMethod = m_formatDesc;
    m_entries.push_back(std::move(ae));

    m_isOpen = true;
    LOG_DBG("DSK: opened %s (%s, %llu bytes)", m_path.c_str(),
            m_formatName.c_str(), (unsigned long long)m_dataSize);
    return true;
}

void DskEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_appleEntries.clear();
    m_diskData.clear();
    m_path.clear();
    m_formatName = "DSK";
    m_formatDesc.clear();
    m_dataSize = 0;
    m_dataOffset = 0;
    m_isAppleDisk = false;
    m_appleTracks = 0;
}

const std::vector<ArchiveEntry>& DskEngine::ListContents()
{
    return m_entries;
}

bool DskEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    auto data = ReadFile(entryName);
    if (data.empty()) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

bool DskEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    if (m_entries.empty()) return false;

    bool allOk = true;
    for (const auto& entry : m_entries)
    {
        if (m_extractCancelled) break;
        if (entry.isDirectory) continue;
        std::string outPath = (fs::path(destPath) / entry.path).string();
        if (!Extract(entry.path, outPath))
            allOk = false;
    }
    return allOk;
}

std::vector<uint8_t> DskEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    if (m_entries.empty())
        return {};

    if (m_isAppleDisk)
    {
        std::string key(entryName);
        const AppleEntry* found = nullptr;
        for (const auto& ae : m_appleEntries)
        {
            if (ae.name == key)
            {
                found = &ae;
                break;
            }
        }
        if (!found) return {};
        if (found->isDirectory) return {};

        if (found->storageType != 0)
            return readProDosFile(found->keyBlock, found->storageType, found->size);
        else
            return readDos33File(found->firstTrack, found->firstSector, found->size);
    }

    if (m_entries[0].name != entryName && m_entries[0].path != entryName)
        return {};

    if (m_diskData.empty())
    {
        FILE* f = std::fopen(m_path.c_str(), "rb");
        if (!f) return {};
        std::vector<uint8_t> data;
        readRaw(f, data);
        std::fclose(f);
        return data;
    }

    return m_diskData;
}

std::vector<uint8_t> DskEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes)
        data.resize(maxBytes);
    return data;
}

bool DskEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;
    if (cancelFlag && cancelFlag()) return false;
    if (progressCallback) progressCallback(0, 1);

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fclose(f);
    return static_cast<uint64_t>(len) >= m_dataOffset + m_dataSize;
}
