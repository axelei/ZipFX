#include "LibarchiveEngine.h"

#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <array>

namespace fs = std::filesystem;

// ── Lifecycle ──────────────────────────────────────────────────────────
LibarchiveEngine::LibarchiveEngine(
    std::vector<FormatRegistrar> registrars,
    const char* formatName,
    bool supportsCreation,
    const char* compressionMethod)
    : m_formatName(formatName)
    , m_compressionMethod(compressionMethod ? compressionMethod : "")
    , m_supportsCreation(supportsCreation)
    , m_registrars(std::move(registrars))
{
}

LibarchiveEngine::~LibarchiveEngine()
{
    Close();
}

void LibarchiveEngine::registerFormat(struct archive* a)
{
    for (auto reg : m_registrars)
        reg(a);
    // Applied on every (re-)open — RAR/7z/ZIP entries encrypted at the
    // libarchive level (RarEngine's non-Bit7z fallback, or a 7z/ZIP opened
    // directly via this engine) need the passphrase before headers/data are
    // read; a no-op if m_password is empty or the format doesn't support it.
    if (!m_password.empty())
        archive_read_add_passphrase(a, m_password.c_str());
}

bool LibarchiveEngine::openArchive(std::string_view path)
{
    if (m_isOpen)
    {
        Close();
    }
    m_path = path;

    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed (out of memory)", m_formatName.c_str());
        return false;
    }
    registerFormat(m_archive);

    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to open %s", m_formatName.c_str(), m_path.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        return false;
    }

    m_isOpen = true;
    LOG_DBG("%s: opened %s", m_formatName.c_str(), m_path.c_str());
    return LoadEntries();
}

bool LibarchiveEngine::Open(std::string_view path)
{
    return openArchive(path);
}

void LibarchiveEngine::Close()
{
    if (m_archive)
    {
        archive_read_free(m_archive);
        m_archive = nullptr;
    }
    m_isOpen = false;
    m_entries.clear();
}

// ── Entry loading ──────────────────────────────────────────────────────
bool LibarchiveEngine::LoadEntries()
{
    m_entries.clear();

    struct archive_entry* entry;
    size_t entryCount = 0;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (isOpenCancelled()) return false;

        if (m_openProgressCb && (entryCount % 100 == 0))
        {
            ArchiveEngine::OpenProgressInfo info;
            info.currentBytes = entryCount;
            info.totalBytes = -1; // Unknown total
            m_openProgressCb(info);
        }

        const char* pathname = archive_entry_pathname(entry);
        if (!pathname)
        {
            archive_read_data_skip(m_archive);
            continue;
        }

        ArchiveEntry ae;
        ae.name = pathname;
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

        ae.compressionMethod = m_compressionMethod;

        m_entries.push_back(std::move(ae));
        entryCount++;
    }

    if (m_openProgressCb)
    {
        ArchiveEngine::OpenProgressInfo info;
        info.currentBytes = entryCount;
        info.totalBytes = entryCount;
        m_openProgressCb(info);
    }

    if (m_entries.size() == 1 && m_entries[0].name == "data")
    {
        fs::path archivePath(m_path);
        std::string stem = archivePath.stem().string();
        if (!stem.empty())
            m_entries[0].name = m_entries[0].path = stem;
    }

    return true;
}

// ── Listing ────────────────────────────────────────────────────────────
const std::vector<ArchiveEntry>& LibarchiveEngine::ListContents()
{
    return m_entries;
}

// ── Extraction ─────────────────────────────────────────────────────────
bool LibarchiveEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;
    std::lock_guard<std::mutex> lock(m_archiveMutex);

    archive_read_free(m_archive);
    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed for Extract (out of memory)", m_formatName.c_str());
        m_isOpen = false;
        return false;
    }
    registerFormat(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to re-open for Extract", m_formatName.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return false;
    }

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            if (!isSafeEntryName(currentName)) return false;
            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());

            std::ofstream out(dest, std::ios::binary);
            if (!out)
            {
                LOG_ERR("%s: cannot create %s", m_formatName.c_str(), dest.string().c_str());
                return false;
            }

            std::array<char, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
            {
                if (m_extractCancelled)
                {
                    out.close();
                    fs::remove(dest);
                    return false;
                }
                out.write(buf.data(), bytesRead);
            }
            bool ok = bytesRead >= 0 && out.good();
            out.close();
            if (!ok)
                fs::remove(dest);
            return ok;
        }

        archive_read_data_skip(m_archive);
    }

    LOG_WARN("%s: entry '%s' not found", m_formatName.c_str(), name.c_str());
    return false;
}

bool LibarchiveEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;
    std::lock_guard<std::mutex> lock(m_archiveMutex);

    // Re-open for a single sequential pass through the archive
    archive_read_free(m_archive);
    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed for ExtractAll (out of memory)", m_formatName.c_str());
        m_isOpen = false;
        return false;
    }
    registerFormat(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to open archive for ExtractAll", m_formatName.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return false;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (m_extractCancelled) { LOG_DBG("%s: extract cancelled", m_formatName.c_str()); return false; }

        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        std::string name(currentName);
        if (!isSafeEntryName(name)) { LOG_WARN("%s: skipping unsafe entry '%s'", m_formatName.c_str(), name.c_str()); archive_read_data_skip(m_archive); continue; }
        bool isDir = archive_entry_filetype(entry) == AE_IFDIR;

        fs::path fullPath = fs::path(destPath) / name;
        if (isDir)
        {
            fs::create_directories(fullPath);
            continue;
        }

        fs::create_directories(fullPath.parent_path());

        std::ofstream out(fullPath, std::ios::binary);
        if (!out)
        {
            LOG_ERR("%s: cannot create %s", m_formatName.c_str(), fullPath.string().c_str());
            archive_read_free(m_archive);
            m_archive = nullptr;
            m_isOpen = false;
            return false;
        }

        std::array<char, 65536> buf;
        la_ssize_t bytesRead;
        while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
        {
            if (m_extractCancelled) { LOG_DBG("%s: extract cancelled", m_formatName.c_str()); return false; }
            out.write(buf.data(), bytesRead);
        }

        if (bytesRead < 0)
        {
            LOG_WARN("%s: archive_read_data error for %s", m_formatName.c_str(), name.c_str());
            archive_read_free(m_archive);
            m_archive = nullptr;
            m_isOpen = false;
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> LibarchiveEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen)
    {
        return {};
    }
    m_extractCancelled = false;
    std::lock_guard<std::mutex> lock(m_archiveMutex);

    archive_read_free(m_archive);
    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed for ReadFile (out of memory)", m_formatName.c_str());
        m_isOpen = false;
        return {};
    }
    registerFormat(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to re-open for ReadFile", m_formatName.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return {};
    }

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
                constexpr la_int64_t kMaxInMemoryFileSize = 512ll * 1024 * 1024;
                if (size > kMaxInMemoryFileSize)
                {
                    LOG_WARN("%s: entry '%s' size %lld exceeds in-memory read limit",
                             m_formatName.c_str(), name.c_str(), (long long)size);
                    return {};
                }
                std::vector<uint8_t> data(static_cast<size_t>(size));
                la_ssize_t bytesRead = archive_read_data(
                    m_archive, data.data(), static_cast<size_t>(size));

                if (bytesRead < 0)
                {
                    LOG_WARN("%s: archive_read_data failed: %s",
                             m_formatName.c_str(), archive_error_string(m_archive));
                    return {};
                }

                data.resize(static_cast<size_t>(bytesRead));
                return data;
            }

            // Streaming read for unknown-size entries (format_raw)
            std::vector<uint8_t> data;
            std::array<uint8_t, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
            {
                if (m_extractCancelled) return {};
                data.insert(data.end(), buf.begin(), buf.begin() + bytesRead);
            }
            if (bytesRead < 0)
            {
                LOG_WARN("%s: archive_read_data failed: %s",
                         m_formatName.c_str(), archive_error_string(m_archive));
                return {};
            }
            return data;
        }

        archive_read_data_skip(m_archive);
    }

    LOG_WARN("%s: entry '%s' not found", m_formatName.c_str(), name.c_str());
    return {};
}

std::vector<uint8_t> LibarchiveEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    std::lock_guard<std::mutex> lock(m_archiveMutex);

    archive_read_free(m_archive);
    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed for ReadFilePartial (out of memory)", m_formatName.c_str());
        m_isOpen = false;
        return {};
    }
    registerFormat(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to re-open for ReadFilePartial", m_formatName.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return {};
    }

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
                ? std::min(static_cast<size_t>(size), maxBytes)
                : maxBytes;

            std::vector<uint8_t> data(readSize);
            la_ssize_t bytesRead = archive_read_data(m_archive, data.data(), readSize);

            if (bytesRead < 0)
            {
                LOG_WARN("%s: archive_read_data failed: %s",
                         m_formatName.c_str(), archive_error_string(m_archive));
                return {};
            }

            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }

        archive_read_data_skip(m_archive);
    }

    return {};
}

bool LibarchiveEngine::ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer)
{
    if (!m_isOpen) return false;
    std::lock_guard<std::mutex> lock(m_archiveMutex);

    archive_read_free(m_archive);
    m_archive = archive_read_new();
    if (!m_archive)
    {
        LOG_ERR("%s: archive_read_new() failed for ReadFileStreamed (out of memory)", m_formatName.c_str());
        m_isOpen = false;
        return false;
    }
    registerFormat(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to re-open for ReadFileStreamed", m_formatName.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return false;
    }

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            std::array<uint8_t, 65536> buf;
            la_ssize_t n;
            while ((n = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
            {
                if (!consumer(buf.data(), static_cast<size_t>(n))) return false;
                if (m_extractCancelled) return false;
            }
            return n >= 0;
        }

        archive_read_data_skip(m_archive);
    }
    return false;
}

// ── Testing ────────────────────────────────────────────────────────────
bool LibarchiveEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    struct archive* a = archive_read_new();
    if (!a)
    {
        LOG_ERR("%s: archive_read_new() failed for TestIntegrity (out of memory)", m_formatName.c_str());
        return false;
    }
    registerFormat(a);

    if (archive_read_open_filename(a, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        archive_read_free(a);
        return false;
    }

    int total = static_cast<int>(std::min<size_t>(m_entries.size(), INT_MAX));
    int current = 0;

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        if (cancelFlag && cancelFlag())
        {
            archive_read_free(a);
            return false;
        }
        if (progressCallback) progressCallback(current, total);

        if (archive_read_data_skip(a) != ARCHIVE_OK)
        {
            archive_read_free(a);
            return false;
        }
        current++;
    }

    archive_read_free(a);
    return true;
}
