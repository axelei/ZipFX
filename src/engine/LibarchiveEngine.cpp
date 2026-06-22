#include "LibarchiveEngine.h"

#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <array>

namespace fs = std::filesystem;

// ── Lifecycle ──────────────────────────────────────────────────────────
LibarchiveEngine::~LibarchiveEngine()
{
    Close();
}

bool LibarchiveEngine::OpenInternal(std::string_view path)
{
    if (m_isOpen)
    {
        Close();
    }
    m_path = path;

    m_archive = archive_read_new();
    RegisterFormat(m_archive);

    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to open %s", FormatName().data(), m_path.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        return false;
    }

    m_isOpen = true;
    LOG_DBG("%s: opened %s", FormatName().data(), m_path.c_str());
    return LoadEntries();
}

void LibarchiveEngine::Close()
{
    LOG_DBG("%s: closing %s", FormatName().data(), m_path.c_str());
    if (m_archive)
    {
        archive_read_close(m_archive);
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
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        ArchiveEntry ae;
        ae.name = archive_entry_pathname(entry);
        ae.path = ae.name;
        ae.size = static_cast<uint64_t>(archive_entry_size(entry));
        ae.packedSize = ae.size;
        ae.isDirectory = archive_entry_filetype(entry) == AE_IFDIR;

        time_t mtime = archive_entry_mtime(entry);
        ae.modified = std::chrono::system_clock::from_time_t(mtime);

        PostProcessEntry(ae);

        m_entries.push_back(std::move(ae));
    }

    return true;
}

// ── Listing ────────────────────────────────────────────────────────────
std::vector<ArchiveEntry> LibarchiveEngine::ListContents()
{
    return m_entries;
}

// ── Extraction ─────────────────────────────────────────────────────────
bool LibarchiveEngine::ExtractAll(std::string_view destPath)
{
    // Single pass through the archive instead of open/close per entry
    archive_read_close(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to open archive for ExtractAll", FormatName().data());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return false;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        std::string name(currentName);
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
            LOG_ERR("%s: cannot create %s", FormatName().data(), fullPath.string().c_str());
            return false;
        }

        std::array<char, 65536> buf;
        la_ssize_t bytesRead;
        while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
        {
            out.write(buf.data(), bytesRead);
        }

        if (bytesRead < 0)
        {
            LOG_WARN("%s: archive_read_data error for %s", FormatName().data(), name.c_str());
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

    // Re-open from the beginning and scan for the requested entry
    archive_read_close(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("%s: failed to re-open for ReadFile", FormatName().data());
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_isOpen = false;
        return {};
    }

    std::string name(entryName);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName)
        {
            la_int64_t size = archive_entry_size(entry);
            if (size <= 0)
            {
                return {};
            }

            std::vector<uint8_t> data(static_cast<size_t>(size));
            la_ssize_t bytesRead = archive_read_data(
                m_archive, data.data(), static_cast<size_t>(size));

            if (bytesRead < 0)
            {
                LOG_WARN("%s: archive_read_data failed: %s",
                         FormatName().data(), archive_error_string(m_archive));
                return {};
            }

            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }

        archive_read_data_skip(m_archive);
    }

    LOG_WARN("%s: entry '%s' not found", FormatName().data(), name.c_str());
    return {};
}

// ── Testing ────────────────────────────────────────────────────────────
bool LibarchiveEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    struct archive* a = archive_read_new();
    RegisterFormat(a);

    if (archive_read_open_filename(a, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        archive_read_free(a);
        return false;
    }

    int total = (int)m_entries.size();
    int current = 0;

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        if (cancelFlag && cancelFlag())
        {
            archive_read_close(a);
            archive_read_free(a);
            return false;
        }
        if (progressCallback) progressCallback(current, total);

        if (archive_read_data_skip(a) != ARCHIVE_OK)
        {
            archive_read_close(a);
            archive_read_free(a);
            return false;
        }
        current++;
    }

    archive_read_close(a);
    archive_read_free(a);
    return true;
}
