#include "GdiEngine.h"
#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

GdiEngine::~GdiEngine()
{
    GdiEngine::Close();
}

bool GdiEngine::parseGdi()
{
    std::ifstream in(m_path);
    if (!in) return false;

    m_tracks.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;

        // Strip carriage return
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        GdiTrack t;
        char filename[1024] = {};

        // Try quoted filename ("track01.bin") first, then bare (track01.bin)
        int n = std::sscanf(line.c_str(), "%d %d %d %d \"%1023[^\"]\" %d",
                            &t.number, &t.lba, &t.sectorType,
                            &t.sectorSize, filename, &t.extOffset);
        if (n < 5)
            n = std::sscanf(line.c_str(), "%d %d %d %d %1023s %d",
                            &t.number, &t.lba, &t.sectorType,
                            &t.sectorSize, filename, &t.extOffset);

        if (n >= 5)
        {
            t.fileName = filename;
            t.resolvedPath = (fs::path(m_baseDir) / t.fileName).string();
            m_tracks.push_back(std::move(t));
        }
    }

    return !m_tracks.empty();
}

bool GdiEngine::Open(std::string_view path)
{
    Close();
    m_path = path;
    m_baseDir = fs::path(m_path).parent_path().string();
    if (m_baseDir.empty()) m_baseDir = ".";

    if (!parseGdi())
    {
        LOG_ERR("GDI: failed to parse %s", m_path.c_str());
        return false;
    }

    // Build ArchiveEntry list with computed sector counts
    m_entries.clear();
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        auto& t = m_tracks[i];

        // Determine sector count for this track
        uint64_t sectors = 0;
        if (i + 1 < m_tracks.size())
        {
            int nextLba = m_tracks[i + 1].lba;
            sectors = (nextLba > t.lba) ? static_cast<uint64_t>(nextLba - t.lba) : 1;
        }
        else
        {
            // Last track: compute from file size
            FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
            if (f)
            {
                std::fseek(f, 0, SEEK_END);
                long fileLen = std::ftell(f);
                std::fclose(f);
                if (fileLen > t.extOffset && t.sectorSize > 0)
                    sectors = static_cast<uint64_t>(fileLen - t.extOffset) / t.sectorSize;
            }
        }

        uint64_t trackSize = sectors * t.sectorSize;

        char nameBuf[64];
        const char* typeStr = "Data";
        if (t.sectorType == 3) typeStr = "Audio";
        else if (t.sectorType == 1) typeStr = "Mode1";
        else if (t.sectorType == 2) typeStr = "Mode2";
        std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d (%s).bin", t.number, typeStr);

        ArchiveEntry ae;
        ae.name = nameBuf;
        ae.path = nameBuf;
        ae.size = trackSize;
        ae.packedSize = trackSize;
        ae.isDirectory = false;
        ae.compressionMethod = t.sectorType == 3 ? "Audio" : "Data";
        m_entries.push_back(std::move(ae));
    }

    m_isOpen = true;
    LOG_DBG("GDI: opened %s (%zu tracks)", m_path.c_str(), m_tracks.size());
    return true;
}

void GdiEngine::Close()
{
    m_isOpen = false;
    m_tracks.clear();
    m_entries.clear();
    m_path.clear();
    m_baseDir.clear();
}

const std::vector<ArchiveEntry>& GdiEngine::ListContents()
{
    return m_entries;
}

static int findTrack(const std::vector<GdiTrack>& tracks,
                     const std::vector<ArchiveEntry>& entries,
                     std::string_view entryName)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].name == entryName || entries[i].path == entryName)
            return static_cast<int>(i);
    }
    return -1;
}

std::vector<uint8_t> GdiEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    int idx = findTrack(m_tracks, m_entries, entryName);
    if (idx < 0) return {};

    const auto& t = m_tracks[idx];

    FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
    if (!f) return {};

    std::fseek(f, 0, SEEK_END);
    long fileLen = std::ftell(f);
    uint64_t available = (fileLen > t.extOffset) ? static_cast<uint64_t>(fileLen - t.extOffset) : 0;
    uint64_t sectors = available / t.sectorSize;
    uint64_t trackSize = sectors * t.sectorSize;

    std::vector<uint8_t> data(trackSize);
    std::fseek(f, t.extOffset, SEEK_SET);
    size_t n = std::fread(data.data(), 1, trackSize, f);
    std::fclose(f);

    if (n != trackSize)
        data.resize(n);
    return data;
}

bool GdiEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    int idx = findTrack(m_tracks, m_entries, entryName);
    if (idx < 0)
    {
        LOG_WARN("GDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
        return false;
    }

    const auto& t = m_tracks[idx];
    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    FILE* src = std::fopen(t.resolvedPath.c_str(), "rb");
    if (!src) return false;

    std::ofstream out(dest, std::ios::binary);
    if (!out) { std::fclose(src); return false; }

    std::fseek(src, t.extOffset, SEEK_SET);
    std::array<char, 65536> buf;
    while (!m_extractCancelled)
    {
        size_t n = std::fread(buf.data(), 1, buf.size(), src);
        if (n == 0) break;
        out.write(buf.data(), n);
        if (!out) break;
    }

    std::fclose(src);
    return out.good();
}

bool GdiEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_extractCancelled) return false;
        fs::path outPath = fs::path(destPath) / m_entries[i].name;
        if (!Extract(m_entries[i].name, outPath.string()))
            return false;
    }
    return true;
}

bool GdiEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;

    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback)
            progressCallback(static_cast<int>(i), static_cast<int>(m_tracks.size()));

        const auto& t = m_tracks[i];
        FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long len = std::ftell(f);
        std::fclose(f);
        if (len <= t.extOffset) return false;
    }
    return true;
}
