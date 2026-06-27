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

// ── GDI descriptor parsing ────────────────────────────────────────────────────

bool GdiEngine::parseGdi()
{
    std::ifstream in(m_path);
    if (!in) return false;

    m_tracks.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        GdiTrack t;
        char filename[1024] = {};

        // Try quoted filename first, then bare
        int n = std::sscanf(line.c_str(), "%d %d %d %d \"%1023[^\"]\" %d",
                            &t.number, &t.lba, &t.sectorType,
                            &t.sectorSize, filename, &t.extOffset);
        if (n < 5)
            n = std::sscanf(line.c_str(), "%d %d %d %d %1023s %d",
                            &t.number, &t.lba, &t.sectorType,
                            &t.sectorSize, filename, &t.extOffset);
        if (n >= 5)
        {
            t.fileName     = filename;
            t.resolvedPath = (fs::path(m_baseDir) / t.fileName).string();
            m_tracks.push_back(std::move(t));
        }
    }

    return !m_tracks.empty();
}

// ── Fallback: expose raw tracks ───────────────────────────────────────────────

void GdiEngine::buildTrackEntries()
{
    m_entries.clear();
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        const auto& t = m_tracks[i];

        uint64_t sectors = 0;
        if (i + 1 < m_tracks.size())
        {
            int nextLba = m_tracks[i + 1].lba;
            sectors = (nextLba > t.lba) ? static_cast<uint64_t>(nextLba - t.lba) : 1;
        }
        else
        {
            FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
            if (f)
            {
                std::fseek(f, 0, SEEK_END);
                long len = std::ftell(f);
                std::fclose(f);
                if (len > t.extOffset && t.sectorSize > 0)
                    sectors = static_cast<uint64_t>(len - t.extOffset) / t.sectorSize;
            }
        }

        uint64_t trackSize = sectors * t.sectorSize;

        char nameBuf[64];
        const char* typeStr = (t.sectorType == 3) ? "Audio" :
                              (t.sectorType == 1) ? "Mode1" :
                              (t.sectorType == 2) ? "Mode2" : "Data";
        std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d (%s).bin", t.number, typeStr);

        ArchiveEntry ae;
        ae.name              = nameBuf;
        ae.path              = nameBuf;
        ae.size              = trackSize;
        ae.packedSize        = trackSize;
        ae.isDirectory       = false;
        ae.compressionMethod = (t.sectorType == 3) ? "Audio" : "Data";
        m_entries.push_back(std::move(ae));
    }
}

// ── ISO 9660 filesystem mounting ─────────────────────────────────────────────

bool GdiEngine::tryMountFilesystem()
{
    // Pick the data track with the highest LBA (the main data area)
    const GdiTrack* best = nullptr;
    for (const auto& t : m_tracks)
    {
        if (t.sectorType == 3) continue; // skip audio
        if (!best || t.lba > best->lba)
            best = &t;
    }
    if (!best) return false;

    m_dataFile = std::fopen(best->resolvedPath.c_str(), "rb");
    if (!m_dataFile) return false;

    // Peek at first bytes to detect raw (2352-byte) vs cooked (2048-byte) sectors
    uint8_t buf[24]{};
    std::fread(buf, 1, sizeof(buf), m_dataFile);
    std::fseek(m_dataFile, 0, SEEK_SET);

    m_dataSectorSize  = static_cast<uint32_t>(best->sectorSize);
    m_dataExtOffset   = static_cast<uint32_t>(best->extOffset);
    m_dataTrackLba    = static_cast<uint32_t>(best->lba);
    if (m_dataSectorSize == 2352)
        m_dataHeaderOff = (buf[15] == 2) ? 24u : 16u;
    else
        m_dataHeaderOff = 0;

    // Build sector reader. LBAs passed in are disc-absolute (as stored in the
    // ISO 9660 structures on a GDI disc); subtract the track's disc-start LBA
    // to get the track-relative sector number, then convert to a file offset.
    auto sectorFn = [this](uint32_t discLba, uint8_t* out) -> bool
    {
        if (discLba < m_dataTrackLba) return false;
        int64_t off = m_dataExtOffset
                    + static_cast<int64_t>(discLba - m_dataTrackLba) * m_dataSectorSize
                    + m_dataHeaderOff;
#ifdef _WIN32
        if (_fseeki64(m_dataFile, off, SEEK_SET) != 0) return false;
#else
        if (fseeko(m_dataFile, static_cast<off_t>(off), SEEK_SET) != 0) return false;
#endif
        return std::fread(out, 1, 2048, m_dataFile) == 2048;
    };

    // VD scan also uses disc-absolute LBAs (trackLba + 16)
    if (!m_iso.open(sectorFn, m_dataTrackLba + 16))
    {
        std::fclose(m_dataFile);
        m_dataFile = nullptr;
        return false;
    }

    // Build ArchiveEntry list from the ISO filesystem
    m_entries.clear();
    for (const auto& e : m_iso.entries())
    {
        ArchiveEntry ae;
        ae.name        = e.path;
        ae.path        = e.path;
        ae.size        = e.size;
        ae.packedSize  = e.size;
        ae.isDirectory = e.isDir;
        if (e.mtime != 0)
            ae.modified = std::chrono::system_clock::from_time_t(e.mtime);
        m_entries.push_back(std::move(ae));
    }

    return true;
}

// ── ArchiveEngine interface ───────────────────────────────────────────────────

bool GdiEngine::Open(std::string_view path)
{
    Close();
    m_path    = std::string(path);
    m_baseDir = fs::path(m_path).parent_path().string();
    if (m_baseDir.empty()) m_baseDir = ".";

    if (!parseGdi())
    {
        LOG_ERR("GDI: failed to parse %s", m_path.c_str());
        return false;
    }

    m_hasFilesystem = tryMountFilesystem();
    if (!m_hasFilesystem)
    {
        buildTrackEntries();
        LOG_DBG("GDI: no filesystem found, showing %zu tracks", m_tracks.size());
    }
    else
    {
        LOG_DBG("GDI: filesystem mounted, %zu entries", m_entries.size());
    }

    m_isOpen = true;
    return true;
}

void GdiEngine::Close()
{
    m_isOpen        = false;
    m_hasFilesystem = false;
    m_tracks.clear();
    m_entries.clear();
    m_iso = Iso9660Reader{};
    if (m_dataFile) { std::fclose(m_dataFile); m_dataFile = nullptr; }
    m_path.clear();
    m_baseDir.clear();
}

const std::vector<ArchiveEntry>& GdiEngine::ListContents()
{
    return m_entries;
}

// ── Read / Extract ────────────────────────────────────────────────────────────

std::vector<uint8_t> GdiEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    if (m_hasFilesystem)
    {
        for (const auto& e : m_iso.entries())
        {
            if (e.isDir || e.path != entryName) continue;

            std::vector<uint8_t> result;
            result.reserve(e.size);
            m_iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool
            {
                result.insert(result.end(), data, data + len);
                return !m_extractCancelled.load();
            });
            return result;
        }
        return {};
    }

    // Track-level fallback ─────────────────────────────────────────────────────
    int idx = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].name == entryName || m_entries[i].path == entryName)
            { idx = static_cast<int>(i); break; }
    if (idx < 0) return {};

    const auto& t = m_tracks[idx];
    FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
    if (!f) return {};

    std::fseek(f, 0, SEEK_END);
    long fileLen  = std::ftell(f);
    uint64_t avail = (fileLen > t.extOffset) ? static_cast<uint64_t>(fileLen - t.extOffset) : 0;
    uint64_t sz    = (avail / t.sectorSize) * t.sectorSize;

    std::vector<uint8_t> data(sz);
    std::fseek(f, t.extOffset, SEEK_SET);
    size_t n = std::fread(data.data(), 1, sz, f);
    std::fclose(f);
    if (n != sz) data.resize(n);
    return data;
}

bool GdiEngine::ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    if (m_hasFilesystem)
    {
        for (const auto& e : m_iso.entries())
        {
            if (e.isDir || e.path != entryName) continue;
            return m_iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                return consumer(data, len) && !m_extractCancelled.load();
            });
        }
        return false;
    }

    // Track-level fallback — read from disk in chunks
    int idx = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].name == entryName || m_entries[i].path == entryName)
            { idx = static_cast<int>(i); break; }
    if (idx < 0) return false;

    const auto& t = m_tracks[idx];
    FILE* f = std::fopen(t.resolvedPath.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, t.extOffset, SEEK_SET);

    std::array<uint8_t, 65536> buf;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
    {
        if (!consumer(buf.data(), n) || m_extractCancelled) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return true;
}

bool GdiEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    if (m_hasFilesystem)
    {
        for (const auto& e : m_iso.entries())
        {
            if (e.isDir || e.path != entryName) continue;

            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());
            std::ofstream out(dest, std::ios::binary);
            if (!out) return false;

            return m_iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool
            {
                out.write(reinterpret_cast<const char*>(data),
                          static_cast<std::streamsize>(len));
                return out.good() && !m_extractCancelled.load();
            });
        }
        return false;
    }

    // Track-level fallback
    int idx = -1;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].name == entryName || m_entries[i].path == entryName)
            { idx = static_cast<int>(i); break; }
    if (idx < 0) return false;

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

    for (const auto& ae : m_entries)
    {
        if (m_extractCancelled) return false;
        if (ae.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / ae.path);
            continue;
        }
        fs::path outPath = fs::path(destPath) / ae.name;
        if (!Extract(ae.name, outPath.string())) return false;
    }
    return true;
}

bool GdiEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;

    if (m_hasFilesystem)
    {
        const auto& entries = m_iso.entries();
        int total = static_cast<int>(entries.size());
        int cur   = 0;
        for (const auto& e : entries)
        {
            if (cancelFlag && cancelFlag()) return false;
            if (progressCallback) progressCallback(cur++, total);
            if (e.isDir) continue;
            if (!m_iso.readData(e.lba, e.size, [](const uint8_t*, size_t) { return true; }))
                return false;
        }
        return true;
    }

    // Track-level fallback
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
