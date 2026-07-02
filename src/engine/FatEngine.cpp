#include "FatEngine.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── FAT12 helpers ─────────────────────────────────────────────────────────────

uint16_t FatEngine::fatEntry(uint16_t cluster) const
{
    // FAT12: each entry is 12 bits.  Two entries packed into 3 bytes.
    uint32_t byteOffset = m_fatStart + (static_cast<uint32_t>(cluster) * 3u / 2u);
    if (byteOffset + 1 >= m_diskData.size()) return 0xFFF;

    uint16_t lo = m_diskData[byteOffset];
    uint16_t hi = m_diskData[byteOffset + 1];

    if (cluster % 2 == 0)
        return lo | ((hi & 0x0F) << 8);
    else
        return (lo >> 4) | (hi << 4);
}

uint64_t FatEngine::clusterOffset(uint16_t cluster) const
{
    return m_dataStart +
           static_cast<uint64_t>(cluster - 2) *
           static_cast<uint64_t>(m_spc) *
           static_cast<uint64_t>(m_bps);
}

std::vector<uint8_t> FatEngine::readClusterChain(uint16_t firstCluster,
                                                   uint32_t fileSize) const
{
    std::vector<uint8_t> result;
    if (firstCluster < 2 || fileSize == 0) return result;
    result.reserve(fileSize);

    uint16_t cur   = firstCluster;
    uint32_t left  = fileSize;
    int      guard = 65536; // safety limit

    while (cur >= 2 && cur < 0xFF8 && guard-- > 0 && left > 0) {
        if (m_extractCancelled) break;

        uint64_t off  = clusterOffset(cur);
        uint32_t clusterBytes = static_cast<uint32_t>(m_spc) * m_bps;
        uint32_t toCopy = (left < clusterBytes) ? left : clusterBytes;

        if (off + toCopy > m_diskData.size()) break;

        result.insert(result.end(),
                      m_diskData.begin() + static_cast<ptrdiff_t>(off),
                      m_diskData.begin() + static_cast<ptrdiff_t>(off + toCopy));
        left -= toCopy;
        cur = fatEntry(cur);
    }

    return result;
}

// ── Directory scanner ─────────────────────────────────────────────────────────
// dirData / dirSize: raw directory bytes.
// pathPrefix: e.g. "SUBDIR/" or "" for root.

void FatEngine::scanDirectory(const uint8_t* dirData, size_t dirSize,
                               const std::string& pathPrefix, int depth)
{
    static constexpr int kEntrySize = 32;
    static constexpr int kMaxDepth = 32;
    if (depth > kMaxDepth) return;

    for (size_t off = 0; off + kEntrySize <= dirSize; off += kEntrySize) {
        const uint8_t* e = dirData + off;

        uint8_t first = e[0];
        if (first == 0x00) break;  // end of directory
        if (first == 0xE5) continue; // deleted entry

        uint8_t attr = e[11];

        // Skip LFN entries (attr == 0x0F) and volume labels (attr & 0x08)
        if (attr == 0x0F)        continue;
        if (attr & 0x08)         continue;

        // Build 8.3 name
        char name8[9] = {}, ext3[4] = {};
        std::memcpy(name8, e,      8);
        std::memcpy(ext3,  e + 8,  3);
        // Strip trailing spaces
        auto stripSp = [](char* s, int n) {
            for (int i = n - 1; i >= 0 && s[i] == ' '; i--) s[i] = '\0';
        };
        stripSp(name8, 8);
        stripSp(ext3,  3);
        if (name8[0] == '\0') continue;

        // Skip "." and ".." entries
        if (std::strcmp(name8, ".") == 0 || std::strcmp(name8, "..") == 0)
            continue;

        // Build display name: "NAME.EXT" (lowercase)
        std::string fname(name8);
        if (ext3[0] != '\0') { fname += '.'; fname += ext3; }
        // Convert to lowercase for friendlier display
        for (auto& c : fname)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::string fullPath = pathPrefix + fname;

        uint16_t firstCluster = static_cast<uint16_t>(e[26]) |
                                 (static_cast<uint16_t>(e[27]) << 8);
        uint32_t fileSize     = static_cast<uint32_t>(e[28]) |
                                 (static_cast<uint32_t>(e[29]) << 8) |
                                 (static_cast<uint32_t>(e[30]) << 16) |
                                 (static_cast<uint32_t>(e[31]) << 24);

        if (attr & 0x10) {
            // Subdirectory
            ArchiveEntry dirEntry;
            dirEntry.name        = fullPath + "/";
            dirEntry.path        = dirEntry.name;
            dirEntry.size        = 0;
            dirEntry.packedSize  = 0;
            dirEntry.isDirectory = true;
            m_entries.push_back(std::move(dirEntry));

            // Read subdirectory cluster chain and recurse
            if (firstCluster >= 2) {
                // Count clusters to determine max size
                std::vector<uint8_t> subData;
                uint16_t cur = firstCluster;
                int guard = 65536;
                while (cur >= 2 && cur < 0xFF8 && guard-- > 0) {
                    uint64_t coff = clusterOffset(cur);
                    uint32_t cSize = static_cast<uint32_t>(m_spc) * m_bps;
                    if (coff + cSize > m_diskData.size()) break;
                    subData.insert(subData.end(),
                                   m_diskData.begin() + static_cast<ptrdiff_t>(coff),
                                   m_diskData.begin() + static_cast<ptrdiff_t>(coff + cSize));
                    cur = fatEntry(cur);
                }
                if (!subData.empty())
                    scanDirectory(subData.data(), subData.size(), fullPath + "/", depth + 1);
            }
        } else {
            // Regular file
            FatEntry fe;
            fe.name         = fullPath;
            fe.firstCluster = firstCluster;
            fe.fileSize     = fileSize;
            m_fatEntries.push_back(fe);

            ArchiveEntry ae;
            ae.name              = fullPath;
            ae.path              = fullPath;
            ae.size              = fileSize;
            ae.packedSize        = fileSize;
            ae.isDirectory       = false;
            ae.compressionMethod = "Uncompressed";
            m_entries.push_back(std::move(ae));
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

FatEngine::~FatEngine()
{
    FatEngine::Close();
}

bool FatEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) { LOG_ERR("FAT: failed to open %s", m_path.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    if (sz < 512) { std::fclose(f); return false; }
    if (sz > 3 * 1024 * 1024) {
        std::fclose(f);
        LOG_WARN("FAT: file too large (%ld bytes) for a FAT12 floppy, skipping %s",
                 sz, m_path.c_str());
        return false;
    }

    m_diskData.resize(static_cast<size_t>(sz));
    size_t n = std::fread(m_diskData.data(), 1, m_diskData.size(), f);
    std::fclose(f);
    m_diskData.resize(n);

    // ── Parse BPB ────────────────────────────────────────────────────────────
    const uint8_t* bpb = m_diskData.data();

    // Jump opcode sanity check
    if (bpb[0] != 0xEB && bpb[0] != 0xE9) {
        LOG_ERR("FAT: no valid BPB jump opcode in %s", m_path.c_str());
        return false;
    }

    m_bps = static_cast<uint16_t>(bpb[11]) | (static_cast<uint16_t>(bpb[12]) << 8);
    if (m_bps != 512) {
        LOG_ERR("FAT: unsupported bytes-per-sector %u in %s", m_bps, m_path.c_str());
        return false;
    }

    m_spc = bpb[13]; // sectors per cluster
    if (m_spc == 0 || (m_spc & (m_spc - 1)) != 0) {
        LOG_ERR("FAT: invalid sectors-per-cluster %u in %s", m_spc, m_path.c_str());
        return false;
    }

    uint16_t reservedSectors = static_cast<uint16_t>(bpb[14]) |
                               (static_cast<uint16_t>(bpb[15]) << 8);
    uint8_t  numFats         = bpb[16];
    m_rootDirEntries         = static_cast<uint16_t>(bpb[17]) |
                               (static_cast<uint16_t>(bpb[18]) << 8);
    uint8_t  mediaDesc       = bpb[21];
    uint16_t sectorsPerFat   = static_cast<uint16_t>(bpb[22]) |
                               (static_cast<uint16_t>(bpb[23]) << 8);

    if (numFats < 1 || numFats > 2) {
        LOG_ERR("FAT: invalid FAT count %u in %s", numFats, m_path.c_str());
        return false;
    }
    if (mediaDesc != 0xF0 && mediaDesc < 0xF8) {
        LOG_ERR("FAT: invalid media descriptor 0x%02X in %s", mediaDesc, m_path.c_str());
        return false;
    }
    if (m_rootDirEntries == 0 || sectorsPerFat == 0) {
        LOG_ERR("FAT: zero root-dir-entries or sectors-per-fat in %s", m_path.c_str());
        return false;
    }

    m_fatStart     = reservedSectors * m_bps;
    m_fatSize      = static_cast<uint32_t>(sectorsPerFat) * m_bps;
    m_rootDirStart = m_fatStart + numFats * m_fatSize;
    uint32_t rootDirBytes = static_cast<uint32_t>(m_rootDirEntries) * 32;
    // Root directory is padded up to a sector boundary
    uint32_t rootDirSectors = (rootDirBytes + m_bps - 1) / m_bps;
    m_dataStart = m_rootDirStart + rootDirSectors * m_bps;

    if (m_fatStart >= m_diskData.size() ||
        m_rootDirStart >= m_diskData.size() ||
        m_dataStart > m_diskData.size()) {
        LOG_ERR("FAT: BPB layout exceeds image size in %s", m_path.c_str());
        return false;
    }

    // ── Scan root directory ───────────────────────────────────────────────────
    size_t rootSize = std::min(static_cast<size_t>(rootDirBytes),
                               m_diskData.size() - m_rootDirStart);
    scanDirectory(m_diskData.data() + m_rootDirStart, rootSize, "");

    m_isOpen = true;
    LOG_DBG("FAT: opened %s (bps=%u spc=%u, %d entries)",
            m_path.c_str(), m_bps, m_spc, static_cast<int>(m_entries.size()));
    return true;
}

void FatEngine::Close()
{
    m_isOpen         = false;
    m_entries.clear();
    m_fatEntries.clear();
    m_diskData.clear();
    m_path.clear();
    m_bps            = 512;
    m_spc            = 1;
    m_fatStart       = 0;
    m_rootDirStart   = 0;
    m_dataStart      = 0;
    m_rootDirEntries = 0;
    m_fatSize        = 0;
}

const std::vector<ArchiveEntry>& FatEngine::ListContents()
{
    return m_entries;
}

bool FatEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    auto data = ReadFile(entryName);
    if (data.empty() && !entryName.empty()) {
        // Check whether it's a zero-byte file
        bool found = false;
        for (const auto& fe : m_fatEntries)
            if (fe.name == entryName) { found = true; break; }
        if (!found) return false;
    }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    if (!data.empty())
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool FatEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    bool allOk = true;
    for (const auto& entry : m_entries) {
        if (m_extractCancelled) break;
        if (entry.isDirectory) {
            fs::create_directories(fs::path(destPath) / entry.path);
            continue;
        }
        std::string outPath = (fs::path(destPath) / entry.path).string();
        if (!Extract(entry.path, outPath))
            allOk = false;
    }
    return allOk;
}

std::vector<uint8_t> FatEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    std::string key(entryName);
    const FatEntry* found = nullptr;
    for (const auto& fe : m_fatEntries) {
        if (fe.name == key) { found = &fe; break; }
    }
    if (!found) return {};

    return readClusterChain(found->firstCluster, found->fileSize);
}

std::vector<uint8_t> FatEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes) data.resize(maxBytes);
    return data;
}

bool FatEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()>         cancelFlag)
{
    if (!m_isOpen) return false;
    if (cancelFlag && cancelFlag()) return false;
    if (progressCallback) progressCallback(0, 1);

    // Verify FAT region is within the image
    bool ok = (m_fatStart + m_fatSize <= m_diskData.size() &&
               m_dataStart <= m_diskData.size());
    if (progressCallback) progressCallback(1, 1);
    return ok;
}
