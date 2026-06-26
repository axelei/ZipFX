#include "D64Engine.h"
#include "Logging.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── D64 geometry ────────────────────────────────────────────────────────────
// Tracks 1-17:  21 sectors each
// Tracks 18-24: 19 sectors each
// Tracks 25-30: 18 sectors each
// Tracks 31-35: 17 sectors each
// Each sector = 256 bytes.  Total = 683 sectors = 174848 bytes (D64).

static int sectorsForTrack(int track)
{
    if (track >= 1  && track <= 17) return 21;
    if (track >= 18 && track <= 24) return 19;
    if (track >= 25 && track <= 30) return 18;
    if (track >= 31 && track <= 35) return 17;
    return 0;
}

// Byte offset of a track's first sector within one D64 side.
int D64Engine::trackOffset(int track)
{
    if (track < 1 || track > 35) return -1;
    int off = 0;
    for (int t = 1; t < track; t++)
        off += sectorsForTrack(t);
    return off * 256;
}

const uint8_t* D64Engine::sectorData(int track, int sector) const
{
    if (track < 1 || track > 35) return nullptr;
    if (sector < 0 || sector >= sectorsForTrack(track)) return nullptr;
    int off = trackOffset(track) + sector * 256;
    if (off < 0 || static_cast<size_t>(off) + 256 > m_diskData.size()) return nullptr;
    return m_diskData.data() + off;
}

// ── Directory parser ─────────────────────────────────────────────────────────

void D64Engine::parseSide(const uint8_t* base, size_t len, const std::string& prefix)
{
    // Directory starts at track 18, sector 1.
    // Each directory sector holds 8 × 32-byte entries.
    // Bytes 0-1 of the FIRST entry in each sector = next track/sector.
    // Follow the chain until next_track == 0.

    int curTrack  = 18;
    int curSector = 1;
    const int kMaxChain = 128; // safety limit
    int guard = 0;

    while (curTrack != 0 && guard++ < kMaxChain) {
        if (curTrack < 1 || curTrack > 35) break;
        if (curSector < 0 || curSector >= sectorsForTrack(curTrack)) break;
        int off = trackOffset(curTrack) + curSector * 256;
        if (off < 0 || static_cast<size_t>(off) + 256 > len) break;

        const uint8_t* sec = base + off;

        // The chain pointer is in bytes 0-1 of the first entry only
        int nextTrack  = sec[0];
        int nextSector = sec[1];

        for (int e = 0; e < 8; e++) {
            const uint8_t* entry = sec + e * 32;

            uint8_t fileType = entry[2];
            // Bit 7 = closed (normal), bits 0-2 = type
            // Skip deleted (type bits == 0) and never-used (fileType == 0x00)
            if (fileType == 0x00) continue;
            if ((fileType & 0x07) == 0) continue;

            // First data track/sector
            int datTrack  = entry[3];
            int datSector = entry[4];
            if (datTrack == 0) continue;

            // Filename: 16 bytes padded with 0xA0, strip trailing 0xA0
            char fname[17] = {};
            int  fnLen     = 0;
            for (int c = 0; c < 16; c++) {
                uint8_t ch = entry[5 + c];
                if (ch == 0xA0) break;
                fname[fnLen++] = static_cast<char>(ch & 0x7F);
            }
            fname[fnLen] = '\0';
            if (fnLen == 0) continue;

            // File size in blocks (bytes 28-29, LE)
            uint16_t blocks = static_cast<uint16_t>(entry[28]) |
                              (static_cast<uint16_t>(entry[29]) << 8);

            std::string entryName = prefix.empty() ? fname
                                                   : prefix + "/" + fname;

            D64Entry de;
            de.name        = entryName;
            de.startTrack  = datTrack;
            de.startSector = datSector;
            de.blocks      = blocks;
            de.baseOffset  = static_cast<size_t>(base - m_diskData.data());
            m_d64Entries.push_back(de);

            ArchiveEntry ae;
            ae.name              = entryName;
            ae.path              = entryName;
            ae.size              = static_cast<uint64_t>(blocks) * 254;
            ae.packedSize        = ae.size;
            ae.isDirectory       = false;
            ae.compressionMethod = "Uncompressed";
            m_entries.push_back(std::move(ae));
        }

        curTrack  = nextTrack;
        curSector = nextSector;
    }
}

// ── File data reader ─────────────────────────────────────────────────────────
// Each D64 data sector: bytes 0-1 = next track/sector (0/x means last),
// bytes 2-255 = 254 data bytes.
// In the final sector: byte 0 = 0, byte 1 = number of bytes used (1-254).

std::vector<uint8_t> D64Engine::readFileData(const uint8_t* base, size_t len,
                                              int startTrack, int startSector) const
{
    std::vector<uint8_t> result;
    result.reserve(4096);

    int curTrack  = startTrack;
    int curSector = startSector;
    const int kMaxBlocks = 8192;
    int guard = 0;

    while (curTrack != 0 && guard++ < kMaxBlocks) {
        if (m_extractCancelled) break;
        if (curTrack < 1 || curTrack > 35) break;
        if (curSector < 0 || curSector >= sectorsForTrack(curTrack)) break;

        int off = trackOffset(curTrack) + curSector * 256;
        if (off < 0 || static_cast<size_t>(off) + 256 > len) break;

        const uint8_t* sec = base + off;
        int nextTrack  = sec[0];
        int nextSector = sec[1];

        if (nextTrack == 0) {
            // Final sector: nextSector == byte count used (1-indexed)
            int used = nextSector;
            if (used < 1 || used > 254) used = 254;
            result.insert(result.end(), sec + 2, sec + 2 + used);
            break;
        }

        // Normal sector: 254 data bytes
        result.insert(result.end(), sec + 2, sec + 256);
        curTrack  = nextTrack;
        curSector = nextSector;
    }

    return result;
}

// ── Public API ───────────────────────────────────────────────────────────────

D64Engine::~D64Engine()
{
    D64Engine::Close();
}

bool D64Engine::Open(std::string_view path)
{
    Close();
    m_path = path;

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) { LOG_ERR("D64: failed to open %s", m_path.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    // Valid sizes: D64 = 174848 or 175531 (with error bytes), D71 = 349696
    if (sz != 174848 && sz != 175531 && sz != 349696) {
        std::fclose(f);
        LOG_ERR("D64: unexpected file size %ld in %s", sz, m_path.c_str());
        return false;
    }

    m_diskData.resize(static_cast<size_t>(sz));
    size_t n = std::fread(m_diskData.data(), 1, m_diskData.size(), f);
    std::fclose(f);
    m_diskData.resize(n);

    m_isD71      = (sz == 349696);
    m_formatName = m_isD71 ? "D71" : "D64";

    // Parse side 1 (always present)
    parseSide(m_diskData.data(), m_diskData.size());

    // Parse side 2 for D71 (starts at byte 174848)
    if (m_isD71 && m_diskData.size() >= 349696) {
        parseSide(m_diskData.data() + 174848, 174848, "Side2");
    }

    m_isOpen = true;
    LOG_DBG("D64: opened %s (%s, %d entries)",
            m_path.c_str(), m_formatName.c_str(), static_cast<int>(m_entries.size()));
    return true;
}

void D64Engine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_d64Entries.clear();
    m_diskData.clear();
    m_path.clear();
    m_formatName = "D64";
    m_isD71      = false;
}

const std::vector<ArchiveEntry>& D64Engine::ListContents()
{
    return m_entries;
}

bool D64Engine::Extract(std::string_view entryName, std::string_view destPath)
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

bool D64Engine::ExtractAll(std::string_view destPath)
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

std::vector<uint8_t> D64Engine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    std::string key(entryName);
    const D64Entry* found = nullptr;
    for (const auto& de : m_d64Entries) {
        if (de.name == key) { found = &de; break; }
    }
    if (!found) return {};

    const uint8_t* base = m_diskData.data() + found->baseOffset;
    size_t         len  = (found->baseOffset == 0)
                          ? (m_isD71 ? 174848 : m_diskData.size())
                          : m_diskData.size() - found->baseOffset;
    if (found->baseOffset + len > m_diskData.size())
        len = m_diskData.size() - found->baseOffset;

    return readFileData(base, len, found->startTrack, found->startSector);
}

std::vector<uint8_t> D64Engine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes) data.resize(maxBytes);
    return data;
}

bool D64Engine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()>         cancelFlag)
{
    if (!m_isOpen) return false;
    if (cancelFlag && cancelFlag()) return false;
    if (progressCallback) progressCallback(0, 1);

    // Basic: verify the disk data was fully read
    bool ok = !m_diskData.empty();
    if (progressCallback) progressCallback(1, 1);
    return ok;
}
