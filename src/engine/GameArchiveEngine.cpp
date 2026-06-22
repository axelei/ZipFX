#include "GameArchiveEngine.h"

#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

GameArchiveEngine::GameArchiveEngine(GameFormat fmt, const char* formatName)
    : m_format(fmt), m_formatName(formatName) {}

GameArchiveEngine::~GameArchiveEngine() { Close(); }

bool GameArchiveEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    uint8_t hdr[64];
    f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (!f) return false;

    // Validate magic
    switch (m_format)
    {
    case GameFormat::Wad:
        if (std::memcmp(hdr, "IWAD", 4) != 0 && std::memcmp(hdr, "PWAD", 4) != 0)
            return false;
        break;
    case GameFormat::Pak:
        if (std::memcmp(hdr, "PACK", 4) != 0) return false;
        break;
    case GameFormat::Grp:
        if (std::memcmp(hdr, "KenSilverman", 12) != 0) return false;
        break;
    }

    // Parse directory
    switch (m_format)
    {
    case GameFormat::Wad:
    {
        int count = static_cast<int>(read32(hdr + 4));
        uint32_t dirOff = read32(hdr + 8);
        for (int i = 0; i < count; ++i)
        {
            uint8_t de[16];
            f.seekg(dirOff + i * 16);
            f.read(reinterpret_cast<char*>(de), 16);
            uint32_t off = read32(de);
            uint32_t sz = read32(de + 4);
            // Name: up to 8 bytes, null-terminated or space-padded
            char name[9] = {};
            std::memcpy(name, de + 8, 8);
            // Trim trailing spaces
            for (int c = 7; c >= 0 && name[c] == ' '; --c) name[c] = '\0';
            if (sz > 0) m_entries.push_back({name, off, sz});
        }
        break;
    }
    case GameFormat::Pak:
    {
        uint32_t dirOff = read32(hdr + 4);
        uint32_t dirSz = read32(hdr + 8);
        int count = static_cast<int>(dirSz / 64);
        for (int i = 0; i < count; ++i)
        {
            uint8_t de[64];
            f.seekg(dirOff + i * 64);
            f.read(reinterpret_cast<char*>(de), 64);
            uint32_t off = read32(de);
            uint32_t sz = read32(de + 4);
            char name[57] = {};
            std::memcpy(name, de + 8, 56);
            name[56] = '\0';
            if (sz > 0) m_entries.push_back({name, off, sz});
        }
        break;
    }
    case GameFormat::Grp:
    {
        int count = static_cast<int>(read32(hdr + 12));
        for (int i = 0; i < count; ++i)
        {
            uint8_t de[20];
            f.read(reinterpret_cast<char*>(de), 20);
            uint32_t off = read32(de);
            uint32_t sz = read32(de + 4);
            char name[13] = {};
            std::memcpy(name, de + 8, 12);
            name[12] = '\0';
            if (sz > 0) m_entries.push_back({name, off, sz});
        }
        break;
    }
    }

    m_isOpen = true;
    LOG_DBG("GameArchiveEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
    return true;
}

void GameArchiveEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
}

std::vector<ArchiveEntry> GameArchiveEngine::ListContents()
{
    std::vector<ArchiveEntry> result;
    result.reserve(m_entries.size());
    for (const auto& e : m_entries)
    {
        ArchiveEntry ae;
        ae.name = e.name;
        ae.path = e.name;
        ae.size = e.size;
        ae.packedSize = e.size;
        ae.isDirectory = false;
        result.push_back(std::move(ae));
    }
    return result;
}

int GameArchiveEngine::findEntry(std::string_view name) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].name == name)
            return static_cast<int>(i);
    return -1;
}

std::vector<uint8_t> GameArchiveEngine::readEntryData(uint32_t offset, uint32_t size) const
{
    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> data(size);
    f.seekg(offset);
    f.read(reinterpret_cast<char*>(data.data()), size);
    if (!f) return {};
    return data;
}

bool GameArchiveEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    int idx = findEntry(entryName);
    if (idx < 0) return false;

    auto data = readEntryData(m_entries[idx].offset, m_entries[idx].size);
    if (data.empty()) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

bool GameArchiveEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
        fs::path dest = fs::path(destPath) / e.name;
        fs::create_directories(dest.parent_path());

        auto data = readEntryData(e.offset, e.size);
        if (data.empty()) return false;

        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    return true;
}

std::vector<uint8_t> GameArchiveEngine::ReadFile(std::string_view entryName)
{
    int idx = findEntry(entryName);
    if (idx < 0) return {};
    return readEntryData(m_entries[idx].offset, m_entries[idx].size);
}

bool GameArchiveEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    int total = static_cast<int>(m_entries.size());
    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(i, total);
    }
    return true;
}
