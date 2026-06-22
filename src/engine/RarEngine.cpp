#include "RarEngine.h"

#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// ── Lifecycle ──────────────────────────────────────────────────────────
RarEngine::~RarEngine()
{
    RarEngine::Close();
}

bool RarEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    m_archive = archive_read_new();
    archive_read_support_format_rar(m_archive);
    archive_read_support_format_rar5(m_archive);

    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("RarEngine: failed to open %s", m_path.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        return false;
    }

    m_isOpen = true;
    LOG_DBG("RarEngine: opened %s", m_path.c_str());
    return LoadEntries();
}

void RarEngine::Close()
{
    LOG_DBG("RarEngine: closing %s", m_path.c_str());
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
bool RarEngine::LoadEntries()
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

        m_entries.push_back(std::move(ae));
    }

    return true;
}

// ── Reading ────────────────────────────────────────────────────────────
std::vector<ArchiveEntry> RarEngine::ListContents()
{
    return m_entries;
}

bool RarEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    auto data = ReadFile(entryName);
    if (data.empty())
    {
        return false;
    }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out)
    {
        return false;
    }

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool RarEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& entry : m_entries)
    {
        if (entry.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / entry.name);
            continue;
        }

        fs::path fullPath = fs::path(destPath) / entry.name;
        fs::create_directories(fullPath.parent_path());

        if (!Extract(entry.name, fullPath.string()))
        {
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> RarEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen)
    {
        return {};
    }

    archive_read_close(m_archive);
    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        LOG_ERR("RarEngine: failed to re-open for ReadFile");
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
                LOG_WARN("RarEngine: archive_read_data failed");
                return {};
            }

            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }

        archive_read_data_skip(m_archive);
    }

    LOG_WARN("RarEngine: entry '%s' not found", name.c_str());
    return {};
}

// ── Writing (not supported for RAR) ────────────────────────────────────
bool RarEngine::AddFile(std::string_view, std::string_view)
{
    return false;
}

bool RarEngine::RemoveEntry(std::string_view)
{
    return false;
}

bool RarEngine::Save()
{
    return false;
}

// ── Testing ────────────────────────────────────────────────────────────
bool RarEngine::TestIntegrity()
{
    struct archive* a = archive_read_new();
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);

    if (archive_read_open_filename(a, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        archive_read_free(a);
        return false;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        if (archive_read_data_skip(a) != ARCHIVE_OK)
        {
            archive_read_close(a);
            archive_read_free(a);
            return false;
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    return true;
}
