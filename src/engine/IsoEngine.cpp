#include "IsoEngine.h"
#include "Logging.h"

#include <archive.h>

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

// ── UDF fallback via libarchive ───────────────────────────────────────────────

static bool probeUdf(const std::string& path)
{
    // Quick check: scan sectors 256-512 for a UDF Volume Recognition Area
    // NSR02 or NSR03 descriptor at sector 256 (0x800 bytes) is the canonical signal.
    // Also check the standard VRA range (16-18) for BEA01/NSR/TEA01.
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    bool found = false;
    uint8_t buf[8];
    // Sectors 16..32 at 2048 bytes each: look for NSR02 or NSR03 at bytes 1-5
    for (int s = 16; s <= 32 && !found; ++s)
    {
        if (std::fseek(f, s * 2048, SEEK_SET) != 0) break;
        if (std::fread(buf, 1, sizeof(buf), f) < 6) break;
        // UDF VRS identifiers start at byte 1; can be "NSR02" or "NSR03"
        if (std::memcmp(buf + 1, "NSR0", 4) == 0 && (buf[5] == '2' || buf[5] == '3'))
            found = true;
    }
    // Also probe sector 256 (used by some DVD/BD mastering tools)
    if (!found)
    {
        if (std::fseek(f, 256 * 2048, SEEK_SET) == 0
            && std::fread(buf, 1, sizeof(buf), f) >= 6
            && std::memcmp(buf + 1, "NSR0", 4) == 0
            && (buf[5] == '2' || buf[5] == '3'))
            found = true;
    }
    std::fclose(f);
    return found;
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

    if (buildSectorReader())
    {
        // Build ArchiveEntry list from ISO 9660 filesystem
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
        LOG_DBG("ISO: opened %s (%zu entries, sectorSize=%u)",
                m_path.c_str(), m_entries.size(), m_sectorSize);
        return true;
    }

    // No ISO 9660 VD found — try UDF via libarchive
    std::fclose(m_file);
    m_file = nullptr;

    if (probeUdf(m_path))
    {
        m_udfFallback = std::make_unique<LibarchiveEngine>(
            std::vector<LibarchiveEngine::FormatRegistrar>{
                archive_read_support_format_iso9660 },
            "ISO/UDF");
        if (m_udfFallback->Open(m_path))
        {
            m_entries = m_udfFallback->ListContents();
            m_isOpen  = true;
            LOG_DBG("ISO: UDF fallback opened %s (%zu entries)",
                    m_path.c_str(), m_entries.size());
            return true;
        }
        m_udfFallback.reset();
    }

    LOG_ERR("ISO: no ISO 9660 or UDF filesystem found in %s", m_path.c_str());
    return false;
}

void IsoEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_iso         = Iso9660Reader{};
    m_udfFallback.reset();
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
        if (e.path == path) return &e;
    return nullptr;
}

// ── Read / Extract ────────────────────────────────────────────────────────────

std::vector<uint8_t> IsoEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;
    if (m_udfFallback) return m_udfFallback->ReadFile(entryName);

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

bool IsoEngine::ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;
    if (m_udfFallback) return m_udfFallback->ReadFileStreamed(entryName, consumer);

    const auto* e = findEntry(entryName);
    if (!e || e->isDir) return false;

    return m_iso.readData(e->lba, e->size, [&](const uint8_t* data, size_t len) -> bool {
        return consumer(data, len) && !m_extractCancelled.load();
    });
}

std::vector<uint8_t> IsoEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;
    if (m_udfFallback) return m_udfFallback->ReadFilePartial(entryName, maxBytes);

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
    if (m_udfFallback) return m_udfFallback->Extract(entryName, destPath);

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
    if (m_udfFallback) return m_udfFallback->ExtractAll(destPath);

    for (const auto& ae : m_entries)
    {
        if (m_extractCancelled) return false;
        if (ae.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / ae.path);
            continue;
        }
        if (!Extract(ae.path, (fs::path(destPath) / ae.path).string())) return false;
    }
    return true;
}

bool IsoEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;
    if (m_udfFallback) return m_udfFallback->TestIntegrity(progressCallback, cancelFlag);

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
