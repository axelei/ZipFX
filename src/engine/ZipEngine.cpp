#include "ZipEngine.h"

#include <wx/log.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static time_t ToTimeT(const std::chrono::system_clock::time_point& tp)
{
    return std::chrono::system_clock::to_time_t(tp);
}

static std::chrono::system_clock::time_point FromTimeT(time_t t)
{
    return std::chrono::system_clock::from_time_t(t);
}

// -----------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------
ZipEngine::~ZipEngine()
{
    ZipEngine::Close();
}

bool ZipEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    if (mz_zip_reader_init_file(&m_archive, m_path.c_str(), 0))
    {
        wxLogDebug("ZipEngine: opened %s", m_path.c_str());
        m_isOpen = true;
        m_isWriter = false;
        m_modified = false;
        LoadEntryCache();
        return true;
    }

    wxLogError("ZipEngine: failed to open %s", m_path.c_str());
    return false;
}

bool ZipEngine::Create(std::string_view path)
{
    Close();
    m_path = path;

    if (mz_zip_writer_init_file(&m_archive, m_path.c_str(), 0))
    {
        wxLogDebug("ZipEngine: created new archive %s", m_path.c_str());
        m_isOpen = true;
        m_isWriter = true;
        m_modified = false;
        LoadEntryCache();
        return true;
    }

    wxLogError("ZipEngine: failed to create %s", m_path.c_str());
    return false;
}

void ZipEngine::Close()
{
    wxLogDebug("ZipEngine: closing %s", m_path.c_str());

    if (m_isWriter)
    {
        mz_zip_writer_end(&m_archive);
    }
    else if (m_isOpen)
    {
        mz_zip_reader_end(&m_archive);
    }

    std::memset(&m_archive, 0, sizeof(m_archive));
    m_isOpen = false;
    m_isWriter = false;
    m_modified = false;
    ClearEntryCache();
}

// -----------------------------------------------------------------------
// Entry cache
// -----------------------------------------------------------------------
void ZipEngine::ClearEntryCache()
{
    m_entries.clear();
}

void ZipEngine::LoadEntryCache()
{
    ClearEntryCache();
    mz_uint numFiles = mz_zip_reader_get_num_files(&m_archive);

    wxLogDebug("ZipEngine: caching %u entries", numFiles);

    for (mz_uint i = 0; i < numFiles; ++i)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&m_archive, i, &stat))
        {
            continue;
        }

        ArchiveEntry entry;
        entry.name = stat.m_filename ? stat.m_filename : "";
        entry.path = entry.name;
        entry.size = static_cast<uint64_t>(stat.m_uncomp_size);
        entry.packedSize = static_cast<uint64_t>(stat.m_comp_size);
        entry.crc = stat.m_crc32;
        entry.isDirectory = mz_zip_reader_is_file_a_directory(&m_archive, i);
        entry.modified = FromTimeT(stat.m_time);

        switch (stat.m_method)
        {
        case MZ_DEFLATED:  entry.compressionMethod = "Deflate"; break;
        case MZ_NO_COMPRESSION: entry.compressionMethod = "Stored";  break;
        default:           entry.compressionMethod = "Unknown"; break;
        }

        m_entries.push_back(std::move(entry));
    }
}

// -----------------------------------------------------------------------
// Reading
// -----------------------------------------------------------------------
std::vector<ArchiveEntry> ZipEngine::ListContents()
{
    return m_entries;
}

bool ZipEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen || m_isWriter)
    {
        return false;
    }

    // Create parent directory using raw string path
    std::string dp(destPath);
    auto slash = dp.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        std::string parent = dp.substr(0, slash);
        fs::create_directories(fs::path(parent));
    }

    mz_bool result = mz_zip_reader_extract_file_to_file(
        &m_archive, entryName.data(), dp.c_str(), 0);

    if (result)
        wxLogDebug("ZipEngine: extracted %s", entryName.data());
    else
        wxLogWarning("ZipEngine: failed to extract %s  →  %s",
                     entryName.data(), dp.c_str());

    return result != 0;
}

bool ZipEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen || m_isWriter)
    {
        return false;
    }

    for (const auto& entry : m_entries)
    {
        if (entry.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / entry.name);
            continue;
        }

        fs::path fullPath = fs::path(destPath) / entry.name;
        fs::create_directories(fullPath.parent_path());

        if (!mz_zip_reader_extract_file_to_file(
                &m_archive, entry.name.c_str(),
                fullPath.string().c_str(), 0))
        {
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> ZipEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen || m_isWriter)
    {
        return {};
    }

    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(
        &m_archive, entryName.data(), &size, 0);

    if (!data)
    {
        return {};
    }

    std::vector<uint8_t> result(
        static_cast<uint8_t*>(data),
        static_cast<uint8_t*>(data) + size);

    mz_free(data);
    return result;
}

// -----------------------------------------------------------------------
// Writing
// -----------------------------------------------------------------------
bool ZipEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    if (!m_isOpen)
    {
        return false;
    }

    m_pendingAdds.push_back({std::string(srcPath), std::string(archivePath)});
    m_modified = true;

    ArchiveEntry placeholder;
    placeholder.name = std::string(archivePath);
    placeholder.path = placeholder.name;
    m_entries.push_back(std::move(placeholder));

    return true;
}

bool ZipEngine::RemoveEntry(std::string_view entryName)
{
    if (!m_isOpen)
    {
        return false;
    }

    m_modified = true;
    return true;
}

bool ZipEngine::Save()
{
    if (!m_isOpen || !m_modified)
    {
        return true;
    }

    // Read existing entries into memory (skip if we're in writer/create mode)
    std::vector<std::vector<uint8_t>> fileData;
    std::vector<std::string> fileNames;

    if (!m_isWriter)
    {
        for (const auto& entry : m_entries)
        {
            auto data = ReadFile(entry.path);
            if (!data.empty())
            {
                fileNames.push_back(entry.path);
                fileData.push_back(std::move(data));
            }
        }

        // Close reader
        mz_zip_reader_end(&m_archive);
        std::memset(&m_archive, 0, sizeof(m_archive));
    }
    else
    {
        // Already in writer mode — flush and reopen as writer
        mz_zip_writer_end(&m_archive);
        std::memset(&m_archive, 0, sizeof(m_archive));
    }

    // Open as writer (overwrite)
    if (!mz_zip_writer_init_file(&m_archive, m_path.c_str(), 0))
    {
        return false;
    }

    m_isWriter = true;

    // Rewrite existing entries
    for (size_t i = 0; i < fileNames.size(); ++i)
    {
        if (!mz_zip_writer_add_mem(
                &m_archive, fileNames[i].c_str(),
                fileData[i].data(), fileData[i].size(),
                MZ_BEST_COMPRESSION))
        {
            return false;
        }
    }

    // Write pending additions
    for (const auto& pf : m_pendingAdds)
    {
        if (!mz_zip_writer_add_file(
                &m_archive, pf.archivePath.c_str(),
                pf.srcPath.c_str(), nullptr, 0,
                MZ_BEST_COMPRESSION))
        {
            wxLogWarning("ZipEngine: can't add %s", pf.srcPath);
        }
    }
    m_pendingAdds.clear();

    m_modified = false;
    return true;
}

// -----------------------------------------------------------------------
// Testing
// -----------------------------------------------------------------------
bool ZipEngine::TestIntegrity()
{
    if (!m_isOpen)
    {
        return false;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&m_archive);

    wxLogDebug("ZipEngine: testing integrity (%u files)", numFiles);

    for (mz_uint i = 0; i < numFiles; ++i)
    {
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&m_archive, i, &size, 0);
        if (!data)
        {
            wxLogWarning("ZipEngine: integrity check failed at entry %u", i);
            return false;
        }
        mz_free(data);
    }

    wxLogDebug("ZipEngine: integrity check passed");
    return true;
}
