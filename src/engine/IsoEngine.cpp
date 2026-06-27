#include "IsoEngine.h"
#include "Logging.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

IsoEngine::~IsoEngine()
{
    IsoEngine::Close();
}

// ── Sector reader setup ───────────────────────────────────────────────────────

bool IsoEngine::buildSectorReader()
{
    // Peek at the first 24 bytes to detect raw vs cooked sectors
    uint8_t buf[24]{};
    if (std::fread(buf, 1, sizeof(buf), m_file) < 24) return false;
    std::fseek(m_file, 0, SEEK_SET);

    detectRawSectorFormat(buf, m_sectorSize, m_headerOff);

    auto sectorFn = [this](uint32_t lba, uint8_t* out) -> bool
    {
        int64_t offset = static_cast<int64_t>(lba) * m_sectorSize + m_headerOff;
#ifdef _WIN32
        if (_fseeki64(m_file, offset, SEEK_SET) != 0) return false;
#else
        if (fseeko(m_file, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
#endif
        return std::fread(out, 1, 2048, m_file) == 2048;
    };

    return m_iso.open(sectorFn);
}

// ── ArchiveEngine interface ───────────────────────────────────────────────────

bool IsoEngine::Open(std::string_view path)
{
    Close();
    m_path = std::string(path);

    m_file = std::fopen(m_path.c_str(), "rb");
    if (!m_file)
    {
        LOG_ERR("ISO: cannot open %s", m_path.c_str());
        return false;
    }

    if (!buildSectorReader())
    {
        LOG_ERR("ISO: no valid ISO 9660 filesystem in %s", m_path.c_str());
        std::fclose(m_file);
        m_file = nullptr;
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

    m_isOpen = true;
    LOG_DBG("ISO: opened %s (%zu entries, sectorSize=%u)", m_path.c_str(),
            m_entries.size(), m_sectorSize);
    return true;
}

void IsoEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_iso    = Iso9660Reader{};
    if (m_file) { std::fclose(m_file); m_file = nullptr; }
    m_path.clear();
}

const std::vector<ArchiveEntry>& IsoEngine::ListContents()
{
    return m_entries;
}

// ── Entry lookup ──────────────────────────────────────────────────────────────

const Iso9660Reader::Entry* IsoEngine::findEntry(std::string_view path) const
{
    for (const auto& e : m_iso.entries())
    {
        if (e.path == path)
            return &e;
    }
    return nullptr;
}

// ── Read / Extract ────────────────────────────────────────────────────────────

std::vector<uint8_t> IsoEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    const auto* e = findEntry(entryName);
    if (!e || e->isDir) return {};

    std::vector<uint8_t> result;
    result.reserve(e->size);

    m_iso.readData(e->lba, e->size, [&](const uint8_t* data, size_t len) -> bool
    {
        result.insert(result.end(), data, data + len);
        return !m_extractCancelled.load();
    });

    return result;
}

std::vector<uint8_t> IsoEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    const auto* e = findEntry(entryName);
    if (!e || e->isDir) return {};

    uint32_t readSize = static_cast<uint32_t>(std::min<uint64_t>(e->size, maxBytes));

    std::vector<uint8_t> result;
    result.reserve(readSize);

    m_iso.readData(e->lba, readSize, [&](const uint8_t* data, size_t len) -> bool
    {
        result.insert(result.end(), data, data + len);
        return !m_extractCancelled.load();
    });

    return result;
}

bool IsoEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    const auto* e = findEntry(entryName);
    if (!e || e->isDir) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;

    bool ok = m_iso.readData(e->lba, e->size, [&](const uint8_t* data, size_t len) -> bool
    {
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
        return out.good() && !m_extractCancelled.load();
    });

    return ok && out.good();
}

bool IsoEngine::ExtractAll(std::string_view destPath)
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
        fs::path outPath = fs::path(destPath) / ae.path;
        if (!Extract(ae.path, outPath.string())) return false;
    }
    return true;
}

bool IsoEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;

    const auto& entries = m_iso.entries();
    int total = static_cast<int>(entries.size());
    int cur   = 0;

    for (const auto& e : entries)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(cur++, total);
        if (e.isDir) continue;

        uint8_t sector[2048];
        uint32_t remaining = e.size;
        uint32_t lba       = e.lba;
        while (remaining > 0)
        {
            if (!m_iso.readData(lba, std::min(remaining, 2048u),
                [](const uint8_t*, size_t) { return true; }))
                return false;
            uint32_t chunk = std::min(remaining, 2048u);
            remaining -= chunk;
            ++lba;
        }
    }
    return true;
}
