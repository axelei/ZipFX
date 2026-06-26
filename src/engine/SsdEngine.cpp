#include "SsdEngine.h"
#include "Logging.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

SsdEngine::~SsdEngine()
{
    SsdEngine::Close();
}

bool SsdEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    // Read whole image into memory
    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) { LOG_ERR("SSD: failed to open %s", m_path.c_str()); return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    if (sz < 512) { std::fclose(f); LOG_ERR("SSD: file too small %s", m_path.c_str()); return false; }

    m_diskData.resize(static_cast<size_t>(sz));
    size_t n = std::fread(m_diskData.data(), 1, m_diskData.size(), f);
    std::fclose(f);
    m_diskData.resize(n);

    if (m_diskData.size() < 512) return false;

    // Extension determines SSD vs DSD
    std::string ext = fs::path(m_path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool isDsd = (ext == ".dsd");
    m_formatName = isDsd ? "DSD" : "SSD";

    // Catalog lives in Track 0:
    //   Sector 0 (bytes   0–255): filename entries
    //   Sector 1 (bytes 256–511): attribute entries
    const uint8_t* s0 = m_diskData.data();
    const uint8_t* s1 = m_diskData.data() + 256;

    // Number of catalog entries encoded in s1[5] bits 7:3
    int numFiles = (s1[5] >> 3) & 0x3F;
    if (numFiles > 31) numFiles = 31; // BBC DFS max is 31 files per side

    for (int i = 0; i < numFiles; i++) {
        const uint8_t* nameAttr = s0 + 8 + i * 8; // 7-char name + dir char
        const uint8_t* meta     = s1 + 8 + i * 8; // 8 attribute bytes

        // Guard against overrun
        if (nameAttr + 8 > m_diskData.data() + 256 ||
            meta     + 8 > m_diskData.data() + 512)
            break;

        // Byte 0 of name slot: null or 0xFF means end / deleted
        if (nameAttr[0] == 0x00 || nameAttr[0] == 0xFF) continue;

        // Filename: 7 bytes, strip bit 7
        char fname[8] = {};
        int  fnLen    = 0;
        for (int c = 0; c < 7; c++) {
            char ch = static_cast<char>(nameAttr[c] & 0x7F);
            fname[c] = ch;
            if (ch != ' ' && ch != '\0') fnLen = c + 1;
        }
        fname[fnLen] = '\0';
        if (fnLen == 0) continue; // space-only name

        // Directory char (bit 7 of nameAttr[7] = locked flag)
        char dirCh = static_cast<char>(nameAttr[7] & 0x7F);
        if (dirCh < ' ' || dirCh == '\0') dirCh = '$';

        // File length from meta[0] bit 4 (high), meta[3] (mid), meta[6] (low)
        uint32_t fileLen =
            (static_cast<uint32_t>((meta[0] >> 4) & 1) << 16) |
            (static_cast<uint32_t>(meta[3]) << 8) |
             static_cast<uint32_t>(meta[6]);

        // Start sector from meta[0] bits 3:2 (high), meta[7] (low)
        uint32_t startSec =
            (static_cast<uint32_t>((meta[0] >> 2) & 3) << 8) |
             static_cast<uint32_t>(meta[7]);

        // Display name: "DIR.FILENAME"
        std::string entryName;
        entryName += dirCh;
        entryName += '.';
        entryName += fname;

        SsdEntry se;
        se.name        = entryName;
        se.startSector = startSec;
        se.size        = fileLen;
        m_ssdEntries.push_back(se);

        ArchiveEntry ae;
        ae.name              = entryName;
        ae.path              = entryName;
        ae.size              = fileLen;
        ae.packedSize        = fileLen;
        ae.isDirectory       = false;
        ae.compressionMethod = "Uncompressed";
        m_entries.push_back(std::move(ae));
    }

    m_isOpen = true;
    LOG_DBG("SSD: opened %s (%s, %d files)",
            m_path.c_str(), m_formatName.c_str(), static_cast<int>(m_entries.size()));
    return true;
}

void SsdEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_ssdEntries.clear();
    m_diskData.clear();
    m_path.clear();
    m_formatName = "SSD";
}

const std::vector<ArchiveEntry>& SsdEngine::ListContents()
{
    return m_entries;
}

bool SsdEngine::Extract(std::string_view entryName, std::string_view destPath)
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

bool SsdEngine::ExtractAll(std::string_view destPath)
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

std::vector<uint8_t> SsdEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    std::string key(entryName);
    const SsdEntry* found = nullptr;
    for (const auto& se : m_ssdEntries) {
        if (se.name == key) { found = &se; break; }
    }
    if (!found) return {};
    if (found->size == 0) return {};

    uint64_t offset = static_cast<uint64_t>(found->startSector) * 256;
    if (offset + found->size > m_diskData.size()) {
        LOG_WARN("SSD: entry '%s' data out of range (offset=%llu size=%u diskSize=%zu)",
                 key.c_str(), (unsigned long long)offset, found->size, m_diskData.size());
        return {};
    }

    return std::vector<uint8_t>(
        m_diskData.begin() + static_cast<ptrdiff_t>(offset),
        m_diskData.begin() + static_cast<ptrdiff_t>(offset + found->size));
}

std::vector<uint8_t> SsdEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    auto data = ReadFile(entryName);
    if (data.size() > maxBytes) data.resize(maxBytes);
    return data;
}

bool SsdEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()>         cancelFlag)
{
    if (!m_isOpen) return false;
    if (cancelFlag && cancelFlag()) return false;
    if (progressCallback) progressCallback(0, 1);

    // Verify each entry's data region is within the image
    for (const auto& se : m_ssdEntries) {
        uint64_t offset = static_cast<uint64_t>(se.startSector) * 256;
        if (offset + se.size > m_diskData.size()) return false;
    }
    if (progressCallback) progressCallback(1, 1);
    return true;
}
