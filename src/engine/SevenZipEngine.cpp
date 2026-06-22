#include "SevenZipEngine.h"

#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// ── Lifecycle ──────────────────────────────────────────────────────────
SevenZipEngine::~SevenZipEngine()
{
    SevenZipEngine::Close();
}

bool SevenZipEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    m_archive = archive_read_new();
    archive_read_support_format_7zip(m_archive);

    if (archive_read_open_filename(m_archive, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        wxLogError("SevenZipEngine: failed to open %s", m_path.c_str());
        archive_read_free(m_archive);
        m_archive = nullptr;
        return false;
    }

    m_isOpen = true;
    wxLogDebug("SevenZipEngine: opened %s", m_path.c_str());
    return LoadEntries();
}

void SevenZipEngine::Close()
{
    wxLogDebug("SevenZipEngine: closing %s", m_path.c_str());
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
bool SevenZipEngine::LoadEntries()
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

        ae.compressionMethod = "LZMA2";

        m_entries.push_back(std::move(ae));
    }

    return true;
}

// ── Reading ────────────────────────────────────────────────────────────
std::vector<ArchiveEntry> SevenZipEngine::ListContents()
{
    return m_entries;
}

bool SevenZipEngine::Extract(std::string_view entryName, std::string_view destPath)
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

bool SevenZipEngine::ExtractAll(std::string_view destPath)
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

std::vector<uint8_t> SevenZipEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen)
    {
        return {};
    }

    // Move back to beginning of archive entries
    archive_read_close(m_archive);
    archive_read_open_filename(m_archive, m_path.c_str(), 10240);

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        std::string currentName = archive_entry_pathname(entry);

        if (currentName == entryName)
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
                return {};
            }

            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }

        // Skip data for non-matching entries
        archive_read_data_skip(m_archive);
    }

    return {};
}

// ── Writing ────────────────────────────────────────────────────────────
bool SevenZipEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    // Stub: 7z writing through libarchive not yet wired up
    (void)srcPath;
    (void)archivePath;
    return false;
}

bool SevenZipEngine::RemoveEntry(std::string_view entryName)
{
    // Stub: modification of existing 7z not yet implemented
    (void)entryName;
    return false;
}

bool SevenZipEngine::Save()
{
    // Stub
    return false;
}

// ── Testing ────────────────────────────────────────────────────────────
bool SevenZipEngine::TestIntegrity()
{
    // Re-open and try to read all entries
    struct archive* a = archive_read_new();
    archive_read_support_format_7zip(a);

    if (archive_read_open_filename(a, m_path.c_str(), 10240) != ARCHIVE_OK)
    {
        archive_read_free(a);
        return false;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        // Try to skip through data
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
