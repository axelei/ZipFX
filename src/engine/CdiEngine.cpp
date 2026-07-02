#include "CdiEngine.h"
#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

static const uint8_t kSyncHeader[12] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

// ── Libarchive callbacks ─────────────────────────────────────────────────

static int cdiOpenCb(struct archive*, void* client_data)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    s->isoPos = 0;
    if (s->file)
        std::fseek(s->file, 0, SEEK_SET);
    return ARCHIVE_OK;
}

static la_ssize_t cdiReadCb(struct archive*, void* client_data, const void** buffer)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    if (!s->file) return ARCHIVE_FATAL;

    uint64_t isoBytes = s->totalSectors * 2048;
    if (s->isoPos >= isoBytes) return 0;

    size_t toRead = static_cast<size_t>(std::min<uint64_t>(isoBytes - s->isoPos, 262144));
    s->buf.resize(toRead);
    size_t bytesRead = 0;

    while (bytesRead < toRead)
    {
        uint64_t isoOff = s->isoPos + bytesRead;
        uint64_t sectorIdx = isoOff / 2048;
        uint64_t sectorOff = isoOff % 2048;
        uint64_t cdiOff = sectorIdx * s->sectorSize + s->seekHeader + sectorOff;

        size_t inSector = static_cast<size_t>(std::min<uint64_t>(2048 - sectorOff, toRead - bytesRead));

        if (fseeko(s->file, static_cast<off_t>(cdiOff), SEEK_SET) != 0)
            break;
        size_t n = std::fread(s->buf.data() + bytesRead, 1, inSector, s->file);
        if (n == 0)
            break;
        bytesRead += n;
        if (n < inSector)
            break;
    }

    s->buf.resize(bytesRead);
    s->isoPos += bytesRead;
    *buffer = s->buf.data();
    return static_cast<la_ssize_t>(bytesRead);
}

static la_int64_t cdiSeekCb(struct archive*, void* client_data,
                            la_int64_t offset, int whence)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    uint64_t isoBytes = s->totalSectors * 2048;

    la_int64_t newPos = 0;
    switch (whence)
    {
    case SEEK_SET: newPos = offset; break;
    case SEEK_CUR: newPos = static_cast<la_int64_t>(s->isoPos) + offset; break;
    case SEEK_END: newPos = static_cast<la_int64_t>(isoBytes) + offset; break;
    default:       return ARCHIVE_FATAL;
    }

    s->isoPos = static_cast<uint64_t>(std::max<la_int64_t>(0, std::min<la_int64_t>(newPos, static_cast<la_int64_t>(isoBytes))));
    return static_cast<la_int64_t>(s->isoPos);
}

static int cdiCloseCb(struct archive*, void* client_data)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    if (s->file)
    {
        std::fclose(s->file);
        s->file = nullptr;
    }
    s->buf.clear();
    s->buf.shrink_to_fit();
    return ARCHIVE_OK;
}

// ── CdiEngine ────────────────────────────────────────────────────────────

CdiEngine::~CdiEngine()
{
    CdiEngine::Close();
}

bool CdiEngine::detectType()
{
    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return false;

    uint8_t buf[16];
    bool ok = false;

    if (std::fread(buf, 1, 16, f) == 16 &&
        std::memcmp(buf, kSyncHeader, 12) == 0)
    {
        m_seekHeader = 16;

        auto probeType = [&](uint32_t probeOffset, uint32_t secSize,
                             uint32_t ecc, Type type) -> bool
        {
            std::fseek(f, long(probeOffset), SEEK_SET);
            uint8_t probe[12];
            if (std::fread(probe, 1, 12, f) == 12 &&
                std::memcmp(probe, kSyncHeader, 12) == 0)
            {
                m_sectorSize = secSize;
                m_seekEcc = ecc;
                m_type = type;
                return true;
            }
            return false;
        };

        if (probeType(2352, 2352, 288, Type::Raw) ||
            probeType(2368, 2368, 304, Type::PQ) ||
            probeType(2448, 2448, 384, Type::CdG))
            ok = true;
    }
    else
    {
        m_seekHeader = 0;
        m_sectorSize = 2048;
        m_seekEcc = 0;
        m_type = Type::Normal;
        ok = true;
    }

    if (ok)
    {
        std::fseek(f, 0, SEEK_END);
        long fileLen = std::ftell(f);
        m_totalSectors = (fileLen > 0) ? uint64_t(fileLen) / m_sectorSize : 0;
    }

    std::fclose(f);
    return ok;
}

bool CdiEngine::openCdiFile()
{
    if (m_cdiFile) return true;
    m_cdiFile = std::fopen(m_path.c_str(), "rb");
    return m_cdiFile != nullptr;
}

void CdiEngine::closeCdiFile()
{
    if (m_cdiFile)
    {
        std::fclose(m_cdiFile);
        m_cdiFile = nullptr;
    }
}

bool CdiEngine::readStrippedSector(uint8_t* out2048)
{
    if (m_seekHeader > 0 && std::fseek(m_cdiFile, m_seekHeader, SEEK_CUR) != 0)
        return false;
    if (std::fread(out2048, 1, 2048, m_cdiFile) != 2048)
        return false;
    if (m_seekEcc > 0 && std::fseek(m_cdiFile, m_seekEcc, SEEK_CUR) != 0)
        return false;
    return true;
}

bool CdiEngine::streamOpen()
{
    m_stream.file = std::fopen(m_path.c_str(), "rb");
    if (!m_stream.file)
    {
        LOG_ERR("CDI: failed to open %s", m_path.c_str());
        return false;
    }

    m_stream.sectorSize = m_sectorSize;
    m_stream.seekHeader = m_seekHeader;
    m_stream.seekEcc = m_seekEcc;
    m_stream.totalSectors = m_totalSectors;
    m_stream.isoPos = 0;

    m_archive = archive_read_new();
    if (!m_archive) { cdiCloseCb(nullptr, &m_stream); return false; }

    archive_read_support_format_iso9660(m_archive);

    archive_read_set_open_callback(m_archive, cdiOpenCb);
    archive_read_set_read_callback(m_archive, cdiReadCb);
    archive_read_set_seek_callback(m_archive, cdiSeekCb);
    archive_read_set_close_callback(m_archive, cdiCloseCb);

    if (archive_read_open1(m_archive) != ARCHIVE_OK)
    {
        LOG_WARN("CDI: ISO-9660 parsing failed: %s — falling back to raw image",
                 archive_error_string(m_archive));
        archive_read_free(m_archive);
        m_archive = nullptr;
        cdiCloseCb(nullptr, &m_stream);
        return false;
    }

    return true;
}

bool CdiEngine::loadAllEntries()
{
    m_entries.clear();

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        ArchiveEntry ae;
        ae.name = archive_entry_pathname(entry);
        ae.path = ae.name;
        la_int64_t rawSize = archive_entry_size(entry);
        ae.size = rawSize > 0 ? static_cast<uint64_t>(rawSize) : 0;
        ae.packedSize = ae.size;
        ae.isDirectory = archive_entry_filetype(entry) == AE_IFDIR;
        ae.permissions = archive_entry_perm(entry) & 0xFFF;
        if (ae.permissions == 0)
            ae.permissions = ae.isDirectory ? 0755 : 0644;

        time_t mtime = archive_entry_mtime(entry);
        ae.modified = std::chrono::system_clock::from_time_t(mtime);

        m_entries.push_back(std::move(ae));
    }

    LOG_DBG("CDI: loaded %zu entries from ISO inside %s",
            m_entries.size(), m_path.c_str());
    return true;
}

bool CdiEngine::ensureArchiveOpen()
{
    if (m_archive || m_fallbackRaw) return true;

    if (m_type == Type::Normal)
    {
        // No CDI wrapping — parse the source file directly
        m_archive = archive_read_new();
        archive_read_support_format_iso9660(m_archive);
        if (archive_read_open_filename(m_archive, m_path.c_str(), 2048) != ARCHIVE_OK)
        {
            LOG_WARN("CDI: failed to open %s as ISO: %s — falling back to raw",
                     m_path.c_str(), archive_error_string(m_archive));
            archive_read_free(m_archive);
            m_archive = nullptr;
            m_fallbackRaw = true;
            ArchiveEntry ae;
            ae.name = "data.iso";
            ae.path = "data.iso";
            ae.size = m_totalSectors * 2048;
            ae.packedSize = ae.size;
            ae.isDirectory = false;
            ae.compressionMethod = "ISO";
            m_entries.push_back(std::move(ae));
            return true;
        }
        return loadAllEntries();
    }

    // Try ISO-9660 via custom callbacks
    if (!streamOpen())
    {
        // Fallback: present the stripped data as a single raw entry
        m_fallbackRaw = true;
        ArchiveEntry ae;
        ae.name = "data.iso";
        ae.path = "data.iso";
        ae.size = m_totalSectors * 2048;
        ae.packedSize = m_totalSectors * m_sectorSize;
        ae.isDirectory = false;
        switch (m_type)
        {
        case Type::Raw:  ae.compressionMethod = "Mode1/RAW";   break;
        case Type::PQ:   ae.compressionMethod = "Mode2/PQ";    break;
        case Type::CdG:  ae.compressionMethod = "CD+G";        break;
        default:         ae.compressionMethod = "ISO";          break;
        }
        m_entries.push_back(std::move(ae));
        LOG_DBG("CDI: raw fallback — single entry data.iso (%llu bytes)",
                (unsigned long long)ae.size);
        return true;
    }

    return loadAllEntries();
}

bool CdiEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    if (!detectType())
    {
        LOG_ERR("CDI: unable to detect format for %s", m_path.c_str());
        return false;
    }

    if (m_totalSectors == 0)
    {
        LOG_ERR("CDI: empty file %s", m_path.c_str());
        return false;
    }

    m_isOpen = true;
    LOG_DBG("CDI: opened %s (%llu sectors, type %d)",
            m_path.c_str(), (unsigned long long)m_totalSectors, (int)m_type);
    return true;
}

void CdiEngine::Close()
{
    if (m_archive)
    {
        archive_read_free(m_archive);
        m_archive = nullptr;
    }
    if (m_stream.file)
    {
        std::fclose(m_stream.file);
        m_stream.file = nullptr;
    }
    closeCdiFile();
    m_stream.buf.clear();
    m_stream.buf.shrink_to_fit();
    m_entries.clear();
    m_fallbackRaw = false;
    m_isOpen = false;
    m_totalSectors = 0;
    m_path.clear();
    m_type = Type::Normal;
    m_sectorSize = 2048;
    m_seekHeader = 0;
    m_seekEcc = 0;
}

const std::vector<ArchiveEntry>& CdiEngine::ListContents()
{
    if (!m_isOpen) { static const std::vector<ArchiveEntry> empty; return empty; }
    if (!ensureArchiveOpen()) { static const std::vector<ArchiveEntry> empty; return empty; }
    return m_entries;
}

// ── Raw fallback reads (no libarchive) ───────────────────────────────────

static std::vector<uint8_t> readRawCdi(FILE* f, uint64_t totalSectors,
                                       uint32_t seekHeader, uint32_t seekEcc)
{
    std::vector<uint8_t> data(totalSectors * 2048);
    std::fseek(f, 0, SEEK_SET);

    for (uint64_t i = 0; i < totalSectors; ++i)
    {
        if (seekHeader > 0 && std::fseek(f, seekHeader, SEEK_CUR) != 0)
        {
            data.resize(i * 2048);
            return data;
        }
        if (std::fread(data.data() + i * 2048, 1, 2048, f) != 2048)
        {
            data.resize(i * 2048);
            return data;
        }
        if (seekEcc > 0 && std::fseek(f, seekEcc, SEEK_CUR) != 0)
        {
            data.resize((i + 1) * 2048);
            return data;
        }
    }
    return data;
}

// ── Extraction ───────────────────────────────────────────────────────────

bool CdiEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    m_extractCancelled = false;

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso")
        {
            LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
            return false;
        }
        if (!openCdiFile()) return false;

        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;

        uint8_t buf[2048];
        for (uint64_t i = 0; i < m_totalSectors; ++i)
        {
            if (m_extractCancelled) { closeCdiFile(); return false; }
            if (!readStrippedSector(buf)) { closeCdiFile(); return false; }
            out.write(reinterpret_cast<const char*>(buf), 2048);
            if (!out) { closeCdiFile(); return false; }
        }
        closeCdiFile();
        return true;
    }

    // ISO mode: re-open and scan for the entry
    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return false;

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());
            std::ofstream out(dest, std::ios::binary);
            if (!out) return false;

            std::array<char, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
            {
                if (m_extractCancelled) return false;
                out.write(buf.data(), bytesRead);
            }
            return bytesRead >= 0 && out.good();
        }
        archive_read_data_skip(m_archive);
    }

    LOG_WARN("CDI: entry '%s' not found", name.c_str());
    return false;
}

bool CdiEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    m_extractCancelled = false;

    if (m_fallbackRaw)
    {
        std::string rawIsoDest = (fs::path(destPath) / "data.iso").string();
return Extract("data.iso", rawIsoDest);
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return false;

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (m_extractCancelled) return false;

        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        std::string name(currentName);
        if (!isSafeEntryName(name)) { LOG_WARN("CdiEngine: skipping unsafe entry '%s'", name.c_str()); continue; }
        bool isDir = archive_entry_filetype(entry) == AE_IFDIR;

        fs::path fullPath = fs::path(destPath) / name;
        if (isDir) { fs::create_directories(fullPath); continue; }
        fs::create_directories(fullPath.parent_path());

        std::ofstream out(fullPath, std::ios::binary);
        if (!out) return false;

        std::array<char, 65536> buf;
        la_ssize_t bytesRead;
        while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
        {
            if (m_extractCancelled) return false;
            out.write(buf.data(), bytesRead);
        }
        if (bytesRead < 0) return false;
    }
    return true;
}

// ── Reading ──────────────────────────────────────────────────────────────

std::vector<uint8_t> CdiEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    if (!ensureArchiveOpen()) return {};

    m_extractCancelled = false;

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso") return {};
        if (!openCdiFile()) return {};
        auto data = readRawCdi(m_cdiFile, m_totalSectors, m_seekHeader, m_seekEcc);
        closeCdiFile();
        return data;
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return {};

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            la_int64_t size = archive_entry_size(entry);
            if (size > 0)
            {
                std::vector<uint8_t> data(static_cast<size_t>(size));
                la_ssize_t bytesRead = archive_read_data(
                    m_archive, data.data(), static_cast<size_t>(size));
                if (bytesRead < 0) return {};
                data.resize(static_cast<size_t>(bytesRead));
                return data;
            }

            std::vector<uint8_t> data;
            std::array<uint8_t, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
                data.insert(data.end(), buf.begin(), buf.begin() + bytesRead);
            if (bytesRead < 0) return {};
            return data;
        }
        archive_read_data_skip(m_archive);
    }

    LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

std::vector<uint8_t> CdiEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    if (!ensureArchiveOpen()) return {};

    m_extractCancelled = false;

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso") return {};
        if (!openCdiFile()) return {};

        uint64_t totalBytes = m_totalSectors * 2048;
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(totalBytes, maxBytes));
        std::vector<uint8_t> data(toRead);

        std::fseek(m_cdiFile, 0, SEEK_SET);
        size_t bytesRead = 0;
        uint8_t buf[2048];
        while (bytesRead < toRead)
        {
            if (!readStrippedSector(buf))
            {
                data.resize(bytesRead);
                break;
            }
            size_t chunk = std::min<size_t>(2048, toRead - bytesRead);
            std::memcpy(data.data() + bytesRead, buf, chunk);
            bytesRead += chunk;
        }

        closeCdiFile();
        data.resize(bytesRead);
        return data;
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return {};

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            la_int64_t size = archive_entry_size(entry);
            size_t readSize = (size > 0)
                ? std::min(static_cast<size_t>(size), maxBytes) : maxBytes;

            std::vector<uint8_t> data(readSize);
            la_ssize_t bytesRead = archive_read_data(m_archive, data.data(), readSize);
            if (bytesRead < 0) return {};
            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }
        archive_read_data_skip(m_archive);
    }
    return {};
}

bool CdiEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    if (m_fallbackRaw)
    {
        if (!openCdiFile()) return false;
        uint8_t buf[2048];
        for (uint64_t i = 0; i < m_totalSectors; ++i)
        {
            if (cancelFlag && cancelFlag()) { closeCdiFile(); return false; }
            if (progressCallback)
                progressCallback(static_cast<int>(i), static_cast<int>(m_totalSectors));
            if (!readStrippedSector(buf)) { closeCdiFile(); return false; }
        }
        closeCdiFile();
        return true;
    }

    int total = static_cast<int>(m_entries.size());
    int current = 0;
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(current, total);
        if (archive_read_data_skip(m_archive) != ARCHIVE_OK) return false;
        current++;
    }
    return true;
}
