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

// ── Sector access ────────────────────────────────────────────────────────
// In a standard Apple II .dsk (DOS-order) file, sectors within each track
// are stored in logical (DOS) order: slot 0 = logical sector 0, slot 1 =
// logical sector 1, etc. No skew translation is needed — the skew table
// only applies to actual rotating-disk hardware, not to .dsk image files.
static uint64_t sectorOffset(int track, int logicalSector)
{
    return static_cast<uint64_t>(track * kAppleSecPerTrack + logicalSector) * kAppleSectorSize;
}

static const uint8_t* sectorPtr(const std::vector<uint8_t>& data, int track, int logicalSector)
{
    uint64_t off = sectorOffset(track, logicalSector);
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

// ── Apple DOS 3.3 parser ─────────────────────────────────────────────────
// VTOC layout (track 17, sector 0):
//   byte 0x00: reserved (not used)
//   byte 0x01: track of first catalog sector
//   byte 0x02: sector of first catalog sector
// Catalog sector link (bytes 0x00-0x02):
//   byte 0x00: reserved
//   byte 0x01: track of next catalog sector (0x00 = last)
//   byte 0x02: sector of next catalog sector
// T/S list sector:
//   byte 0x00: reserved
//   byte 0x01: track of next T/S list sector
//   byte 0x02: sector of next T/S list sector
//   bytes 0x0C+: T/S pairs (track byte, then sector byte), 122 pairs max

bool DskEngine::parseDos33()
{
    // VTOC: track 17, logical sector 0
    const uint8_t* vtoc = sectorPtr(m_diskData, 17, 0);
    if (!vtoc) return false;

    // Byte 0x01 = catalog track; must be 17 (0x11)
    if (vtoc[1] != 0x11) return false;

    int catTrack  = vtoc[1];  // always 17
    int catLogSec = vtoc[2];  // catalog first sector

    // Parse one 35-byte catalog entry; add to m_appleEntries if valid
    auto parseEntry = [&](const uint8_t* e) {
        if (e[0] == 0xFF || e[0] == 0x00) return;  // deleted / never-used
        int ft = e[0], fs = e[1];
        if (ft == 17 || ft > 34 || fs >= 16) return;
        char nameBuf[31]; int nameLen = 0;
        for (int c = 0; c < 30; c++) {
            char ch = static_cast<char>(e[3 + c] & 0x7F);
            if (ch == ' ' || ch == '\0') break;
            nameBuf[nameLen++] = ch;
        }
        if (nameLen == 0) return;
        nameBuf[nameLen] = '\0';
        uint16_t numSec = static_cast<uint16_t>(e[0x21]) |
                          (static_cast<uint16_t>(e[0x22]) << 8);
        if (numSec == 0 || numSec > 560) return;
        uint32_t fileSize = static_cast<uint32_t>(numSec > 1 ? numSec - 1 : 1) * kAppleSectorSize;
        AppleEntry ae;
        ae.name = nameBuf; ae.size = fileSize; ae.fileType = e[2];
        ae.firstTrack = static_cast<uint8_t>(ft);
        ae.firstSector = static_cast<uint8_t>(fs);
        m_appleEntries.push_back(ae);
    };

    bool visited[16] = {};

    // Follow catalog chain
    int t = catTrack;
    int s = catLogSec;
    for (int guard = 0; guard < 20; guard++)
    {
        if (t != 17 || s < 0 || s >= 16) break;
        if (visited[s]) break;
        visited[s] = true;

        const uint8_t* sec = sectorPtr(m_diskData, t, s);
        if (!sec) break;

        int nextT = sec[1];
        int nextS = sec[2];

        for (int i = 0; i < 7; i++)
            parseEntry(sec + 0x0B + i * 35);

        if (nextT == 0 || nextT > 34 || nextS >= 16) break;
        t = nextT;
        s = nextS;
    }

    // Fallback: scan all unvisited logical sectors on track 17
    for (int ls = 0; ls < 16; ls++)
    {
        if (visited[ls]) continue;
        const uint8_t* sec = sectorPtr(m_diskData, 17, ls);
        if (!sec) continue;

        // Quick check: does this sector look like a catalog sector?
        bool hasEntries = false;
        for (int i = 0; i < 7; i++) {
            const uint8_t* e = sec + 0x0B + i * 35;
            if (e[0] != 0xFF && e[0] != 0x00 && e[0] <= 34) {
                for (int c = 0; c < 30; c++) {
                    uint8_t ch = e[3 + c] & 0x7F;
                    if (ch > ' ') { hasEntries = true; break; }
                }
            }
            if (hasEntries) break;
        }
        if (!hasEntries) continue;

        for (int i = 0; i < 7; i++)
            parseEntry(sec + 0x0B + i * 35);
    }

    // Deduplicate by name
    for (int i = static_cast<int>(m_appleEntries.size()) - 1; i > 0; i--) {
        for (int j = 0; j < i; j++) {
            if (m_appleEntries[j].name == m_appleEntries[i].name) {
                m_appleEntries.erase(m_appleEntries.begin() + i);
                break;
            }
        }
    }

    if (m_appleEntries.empty()) return false;
    m_formatDesc = "Apple DOS 3.3 (" + std::to_string(m_appleEntries.size()) + " files)";
    return true;
}

// ── Apple ProDOS parser ──────────────────────────────────────────────────
// ProDOS block layout for directory blocks:
//   bytes 0x00-0x01: backward link (prev dir block)
//   bytes 0x02-0x03: forward link (next dir block)
//   bytes 0x04+: directory entries (kProDOSEntrySize=39 bytes each)
//
// Volume directory header (first entry in block 2, at offset 0x04):
//   e[0x00]: 0xFN (storage type F = volume dir, N = name length)
//   e[0x01-0x0F]: volume name
//   e[0x10-0x11]: reserved
//   e[0x12-0x13]: creation date
//   e[0x14-0x15]: creation time
//   e[0x16]: version, e[0x17]: min_version, e[0x18]: access
//   e[0x19]: entry_length (39), e[0x1A]: entries_per_block (13)
//   e[0x1B-0x1C]: file_count
//   e[0x1D-0x1E]: bitmap_pointer, e[0x1F-0x20]: total_blocks
//
// File entry:
//   e[0x00]: storage_type (high nibble) + name_len (low nibble)
//   e[0x01-0x0F]: file_name (name_len chars)
//   e[0x10]: file_type
//   e[0x11-0x12]: key_pointer (LE)
//   e[0x13-0x14]: blocks_used (LE)
//   e[0x15-0x17]: EOF / actual file size (3-byte LE)
//
// Index block format for sapling/tree files:
//   bytes 0x000-0x0FF: low bytes of 256 block numbers
//   bytes 0x100-0x1FF: high bytes of 256 block numbers

bool DskEngine::parseProDos()
{
    int totalBlocks = static_cast<int>(m_diskData.size()) / kProDOSBlockSize;
    if (totalBlocks < 3) return false;

    auto blockData = [&](int bn) -> const uint8_t* {
        if (bn < 0 || bn >= totalBlocks) return nullptr;
        return m_diskData.data() + static_cast<int64_t>(bn) * kProDOSBlockSize;
    };

    // Block 2 = first volume directory block
    const uint8_t* blk = blockData(kProDOSVolDirBlock);
    if (!blk) return false;

    // Volume directory header is the first entry at offset 4
    const uint8_t* hdr = blk + 4;
    if ((hdr[0] & 0xF0) != kProDosVolumeDir) return false;

    uint8_t entryLen       = hdr[0x19];  if (entryLen == 0) entryLen = kProDOSEntrySize;
    uint8_t entriesPerBlk  = hdr[0x1A];  if (entriesPerBlk == 0) entriesPerBlk = 13;
    int fileCount          = static_cast<int>(hdr[0x1B]) | (static_cast<int>(hdr[0x1C]) << 8);
    if (fileCount <= 0) return false;

    int found = 0;
    int curBlock = kProDOSVolDirBlock;

    while (curBlock != 0 && found < fileCount)
    {
        blk = blockData(curBlock);
        if (!blk) break;

        // File entries start at offset 4 in the first block (after the header entry),
        // or at offset 4 in subsequent blocks (after the first entry which is also the header).
        // More precisely: 4 (link ptrs) + 39*0 = 4 for first block (skip the header).
        // Actually the header occupies the first entry slot, so file entries start at:
        //   offset 4 + entryLen  (skip link pointers, skip the header entry)
        // For blocks after the first: entries start right at offset 4.
        int startOff = (curBlock == kProDOSVolDirBlock) ? (4 + entryLen) : 4;

        for (int off = startOff; off + entryLen <= kProDOSBlockSize && found < fileCount;
             off += entryLen)
        {
            const uint8_t* e = blk + off;
            uint8_t se = e[0];
            if (se == 0x00) continue;  // deleted or never-used slot

            uint8_t st      = se & 0xF0;
            int fnameLen    = se & 0x0F;
            if (fnameLen == 0 || fnameLen > 15) continue;

            if (st != kProDosSeedling && st != kProDosSapling &&
                st != kProDosTree    && st != kProDosSubdir)
                continue;

            char nameBuf[16];
            for (int c = 0; c < fnameLen; c++)
                nameBuf[c] = static_cast<char>(e[1 + c]);
            nameBuf[fnameLen] = '\0';

            bool   isDir    = (st == kProDosSubdir);
            uint8_t  ftype  = e[0x10];
            uint16_t kblk   = static_cast<uint16_t>(e[0x11]) |
                              (static_cast<uint16_t>(e[0x12]) << 8);
            uint32_t fsize  = static_cast<uint32_t>(e[0x15]) |
                              (static_cast<uint32_t>(e[0x16]) << 8) |
                              (static_cast<uint32_t>(e[0x17]) << 16);

            AppleEntry ae;
            ae.name        = nameBuf;
            ae.size        = fsize;
            ae.fileType    = ftype;
            ae.isDirectory = isDir;
            ae.keyBlock    = kblk;
            ae.storageType = st;
            m_appleEntries.push_back(ae);
            found++;
        }

        // Follow forward link to next directory block
        curBlock = static_cast<int>(blk[2]) | (static_cast<int>(blk[3]) << 8);
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
// firstTrack/firstSector = location of the first T/S list sector.
// T/S list layout:
//   byte 0x00: reserved
//   byte 0x01: track of next T/S list sector (0 = last)
//   byte 0x02: sector of next T/S list sector
//   bytes 0x0C-0xFF: (track, sector) pairs of data sectors, 122 pairs max

std::vector<uint8_t> DskEngine::readDos33File(
    uint8_t firstTrack, uint8_t firstSector, uint32_t totalSize) const
{
    if (m_diskData.empty() || totalSize == 0)
        return {};

    std::vector<uint8_t> result;
    result.reserve(totalSize);

    int t = firstTrack;
    int s = firstSector;

    for (int tsGuard = 0; tsGuard < 100 && result.size() < totalSize; tsGuard++)
    {
        if (t <= 0 || t >= m_appleTracks) break;
        if (s < 0 || s >= kAppleSecPerTrack) break;

        const uint8_t* tsSec = sectorPtr(m_diskData, t, s);
        if (!tsSec) break;

        int nextT = tsSec[0x01];
        int nextS = tsSec[0x02];

        // Read the 122 (track, sector) pairs starting at offset 0x0C
        for (int pairIdx = 0; pairIdx < 122 && result.size() < totalSize; pairIdx++)
        {
            int pairOff = 0x0C + pairIdx * 2;
            int dataT = tsSec[pairOff];
            int dataS = tsSec[pairOff + 1];

            if (dataT == 0 && dataS == 0) break;  // end of list
            if (dataT >= m_appleTracks || dataS >= kAppleSecPerTrack) break;

            const uint8_t* dataSec = sectorPtr(m_diskData, dataT, dataS);
            if (!dataSec) break;

            uint32_t remaining = totalSize - static_cast<uint32_t>(result.size());
            uint32_t toCopy = (remaining < kAppleSectorSize) ? remaining : kAppleSectorSize;
            result.insert(result.end(), dataSec, dataSec + toCopy);
        }

        if (nextT == 0 || nextT >= m_appleTracks || nextS >= kAppleSecPerTrack) break;
        t = nextT;
        s = nextS;
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
        // Index block: bytes 0x000-0x0FF are low bytes, 0x100-0x1FF are high bytes
        auto indexBlock = readBlock(keyBlock);
        if (indexBlock.empty()) break;
        for (int i = 0; i < 256; i++)
        {
            uint16_t bn = static_cast<uint16_t>(indexBlock[i]) |
                          (static_cast<uint16_t>(indexBlock[i + 256]) << 8);
            if (bn == 0) break;
            if (bn >= numBlocks) continue;
            dataBlocks.push_back(bn);
        }
        break;
    }

    case kProDosTree:
    {
        // Master index block: same split format as sapling index
        auto masterBlock = readBlock(keyBlock);
        if (masterBlock.empty()) break;
        for (int i = 0; i < 256; i++)
        {
            uint16_t idxBn = static_cast<uint16_t>(masterBlock[i]) |
                             (static_cast<uint16_t>(masterBlock[i + 256]) << 8);
            if (idxBn == 0) break;
            if (idxBn >= numBlocks) continue;

            auto indexBlock = readBlock(static_cast<int>(idxBn));
            if (indexBlock.empty()) break;
            for (int j = 0; j < 256; j++)
            {
                uint16_t bn = static_cast<uint16_t>(indexBlock[j]) |
                              (static_cast<uint16_t>(indexBlock[j + 256]) << 8);
                if (bn == 0) break;
                if (bn < numBlocks)
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
