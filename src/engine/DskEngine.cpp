#include "DskEngine.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── Format detection ─────────────────────────────────────────────────────

enum class DskFormat { Unknown, Raw, D64, D71, D80, D82, CpcDsk, Apple2Img, AppleDc42 };

struct DskProbe {
    DskFormat fmt;
    const char* name;
    const char* desc;
    bool (*match)(const uint8_t* hdr, size_t len, long fileSize);
};

static const DskProbe kProbes[] = {
    // Amstrad CPC DSK — "MV - CPC" or "EXTENDED CPC DSK"
    { DskFormat::CpcDsk, "DSK (CPC)", "Amstrad CPC disk image",
        [](const uint8_t* d, size_t len, long) -> bool {
            if (len < 8) return false;
            return (std::memcmp(d, "MV - CPC", 8) == 0) ||
                   (std::memcmp(d, "EXTENDED", 8) == 0 &&
                    len >= 16 && std::memcmp(d + 8, " CPC", 4) == 0);
        }},
    // Apple 2IMG — "2IMG"
    { DskFormat::Apple2Img, "DSK (2IMG)", "Apple II 2IMG disk image",
        [](const uint8_t* d, size_t len, long) -> bool {
            return len >= 4 && std::memcmp(d, "2IMG", 4) == 0;
        }},
    // Apple Disk Copy 4.2 — starts with 0x01 0x00 followed by "D" at offset 2
    { DskFormat::AppleDc42, "DC42", "Apple Disk Copy 4.2",
        [](const uint8_t* d, size_t len, long) -> bool {
            return len >= 3 && d[0] == 0x01 && d[1] == 0x00 && d[2] == 'D';
        }},
    // Commodore D64 — commonly 174848 bytes (35 tracks) or 196608 (40 tracks)
    // Also detect by first 5 zero bytes + non-zero at offset 5
    { DskFormat::D64, "D64", "Commodore 64 disk image",
        [](const uint8_t* d, size_t len, long fileSize) -> bool {
            if (fileSize != 174848 && fileSize != 175531 &&
                fileSize != 196608 && fileSize != 197376 &&
                fileSize != 201600 && fileSize != 205800)
            {
                if (len < 6) return false;
                // D64 typically starts with first directory track header
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

    // Try each probe
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

    // Fallback: raw / unknown
    m_formatName = "Raw Disk";
    m_formatDesc = "Unrecognized disk image";
    m_dataOffset = 0;
    m_dataSize = static_cast<uint64_t>(fileSize);
    return fileSize > 0;
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
    m_path.clear();
    m_formatName = "DSK";
    m_formatDesc.clear();
    m_dataSize = 0;
    m_dataOffset = 0;
}

const std::vector<ArchiveEntry>& DskEngine::ListContents()
{
    return m_entries;
}

bool DskEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    if (m_entries.empty() ||
        (m_entries[0].name != entryName && m_entries[0].path != entryName))
        return false;

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
    std::string outPath = (fs::path(destPath) / m_entries[0].name).string();
    return Extract(m_entries[0].name, outPath);
}

std::vector<uint8_t> DskEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    if (m_entries.empty() ||
        (m_entries[0].name != entryName && m_entries[0].path != entryName))
        return {};

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return {};

    std::vector<uint8_t> data;
    readRaw(f, data);
    std::fclose(f);
    return data;
}

std::vector<uint8_t> DskEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    if (m_entries.empty() ||
        (m_entries[0].name != entryName && m_entries[0].path != entryName))
        return {};

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return {};

    size_t toRead = static_cast<size_t>(std::min<uint64_t>(m_dataSize, maxBytes));
    std::vector<uint8_t> data(toRead);
    std::fseek(f, static_cast<long>(m_dataOffset), SEEK_SET);
    size_t n = std::fread(data.data(), 1, toRead, f);
    std::fclose(f);
    data.resize(n);
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
