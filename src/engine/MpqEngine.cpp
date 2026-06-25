#include "MpqEngine.h"

#include "Logging.h"

// Ensure TCHAR = char (StormLib was likely built ANSI, not Unicode)
#undef UNICODE
#undef _UNICODE

#include <StormLib.h>

// Map 0-9 compression level to StormLib compression types
static int mpqCompressionType(int level);

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

MpqEngine::MpqEngine() = default;

MpqEngine::~MpqEngine()
{
    Close();
}

// Helper: convert forward slashes to backslashes for StormLib API calls.
// StormLib expects backslash-separated paths internally, but we store
// entries with forward slashes for consistency with other engines.
static std::string toStormPath(std::string_view name)
{
    std::string result(name);
    for (auto& c : result)
        if (c == '/') c = '\\';
    return result;
}

// ── Lifecycle ──────────────────────────────────────────────────────────

bool MpqEngine::Open(std::string_view path)
{
    Close();

    std::string spath(path);

    HANDLE hMpq = nullptr;
    if (!SFileOpenArchive(spath.c_str(), 0, 0, &hMpq))
    {
        LOG_ERR("MpqEngine: failed to open %s", spath.c_str());
        return false;
    }

    m_handle = hMpq;
    m_path = spath;
    m_modified = false;
    reloadEntries();

    m_isOpen = true;
    LOG_DBG("MpqEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
    return true;
}

bool MpqEngine::Create(std::string_view path)
{
    Close();

    std::string spath(path);

    HANDLE hMpq = nullptr;
    if (!SFileCreateArchive(
            spath.c_str(),
            MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2,
            100,
            &hMpq))
    {
        LOG_ERR("MpqEngine: failed to create %s", spath.c_str());
        return false;
    }

    m_handle = hMpq;
    m_path = spath;
    m_isOpen = true;
    m_modified = false;
    m_entries.clear();
    m_pendingAdds.clear();

    LOG_DBG("MpqEngine: created %s", m_path.c_str());
    return true;
}

void MpqEngine::Close()
{
    if (m_handle)
    {
        SFileCloseArchive(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    m_isOpen = false;
    m_modified = false;
    m_entries.clear();
    m_pendingAdds.clear();
}

// ── Entry cache ────────────────────────────────────────────────────────

void MpqEngine::reloadEntries()
{
    m_entries.clear();
    auto* hMpq = static_cast<HANDLE>(m_handle);
    if (!hMpq) return;

    // Filter: skip internal StormLib files (names enclosed in parentheses)
    auto isInternalFile = [](const std::string& name) -> bool {
        return name.size() > 2 && name.front() == '(' && name.back() == ')';
    };

    // Find files via wildcard search
    SFILE_FIND_DATA findData = {};
    HANDLE hFind = SFileFindFirstFile(hMpq, "*", &findData, nullptr);
    if (hFind)
    {
        do
        {
            std::string name = findData.cFileName;
            if (isInternalFile(name)) continue;

            ArchiveEntry ae;
            ae.name = name;
            for (auto& c : ae.name) if (c == '\\') c = '/';
            ae.path = ae.name;
            ae.size = findData.dwFileSize;
            ae.packedSize = findData.dwCompSize;
            ae.isDirectory = false;
            m_entries.push_back(std::move(ae));
        }
        while (SFileFindNextFile(hFind, &findData));
        SFileFindClose(hFind);
    }

    // If wildcard search didn't work, try parsing (listfile)
    if (m_entries.empty())
    {
        HANDLE hFile = nullptr;
        if (SFileOpenFileEx(hMpq, "(listfile)", SFILE_OPEN_FROM_MPQ, &hFile))
        {
            std::vector<char> listData;
            char buf[4096];
            DWORD read = 0;
            while (SFileReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
                listData.insert(listData.end(), buf, buf + read);
            SFileCloseFile(hFile);

            std::string current;
            for (char c : listData)
            {
                if (c == '\r') continue;
                if (c == '\n')
                {
                    if (!current.empty())
                    {
                        if (!isInternalFile(current))
                        {
                            ArchiveEntry ae;
                            ae.name = current;
                            for (auto& c2 : ae.name) if (c2 == '\\') c2 = '/';
                            ae.path = ae.name;
                            m_entries.push_back(std::move(ae));
                        }
                        current.clear();
                    }
                }
                else
                    current += c;
            }
            if (!current.empty() && !isInternalFile(current))
            {
                ArchiveEntry ae;
                ae.name = current;
                for (auto& c2 : ae.name) if (c2 == '\\') c2 = '/';
                ae.path = ae.name;
                m_entries.push_back(std::move(ae));
            }
        }
    }

    // Get sizes for entries found via listfile
    for (auto& e : m_entries)
    {
        if (e.size > 0) continue;

        HANDLE hFile = nullptr;
        std::string stormPath = e.name;
        for (auto& c : stormPath) if (c == '/') c = '\\';
        if (SFileOpenFileEx(hMpq, stormPath.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
        {
            e.size = SFileGetFileSize(hFile, nullptr);
            DWORD cmpSize = 0;
            DWORD cmpSizeLen = sizeof(cmpSize);
            if (SFileGetFileInfo(hFile, SFileInfoCompressedSize, &cmpSize, cmpSizeLen, &cmpSizeLen))
                e.packedSize = cmpSize;

            // Get CRC32 from (attributes) if available
            DWORD crc32 = 0;
            DWORD crcLen = sizeof(crc32);
            if (SFileGetFileInfo(hFile, SFileInfoCRC32, &crc32, crcLen, &crcLen))
                e.crc = crc32;

            SFileCloseFile(hFile);
        }
    }
}

// ── Reading ────────────────────────────────────────────────────────────

const std::vector<ArchiveEntry>& MpqEngine::ListContents()
{
    return m_entries;
}

int MpqEngine::findEntry(std::string_view name) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].path == name)
            return static_cast<int>(i);
    return -1;
}

bool MpqEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen || !m_handle) return false;
    m_extractCancelled = false;

    auto* hMpq = static_cast<HANDLE>(m_handle);
    std::string name = toStormPath(entryName);

    HANDLE hFile = nullptr;
    if (!SFileOpenFileEx(hMpq, name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
        return false;

    DWORD fileSize = SFileGetFileSize(hFile, nullptr);
    if (fileSize == SFILE_INVALID_SIZE)
    {
        SFileCloseFile(hFile);
        return false;
    }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) { SFileCloseFile(hFile); return false; }

    constexpr DWORD kChunk = 65536;
    char buf[kChunk];
    DWORD remaining = fileSize;
    while (remaining > 0)
    {
        if (m_extractCancelled) { SFileCloseFile(hFile); return false; }
        DWORD toRead = std::min(remaining, kChunk);
        DWORD read = 0;
        if (!SFileReadFile(hFile, buf, toRead, &read, nullptr) || read == 0)
        {
            SFileCloseFile(hFile);
            return false;
        }
        out.write(buf, read);
        remaining -= read;
    }

    SFileCloseFile(hFile);
    return out.good();
}

bool MpqEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("MpqEngine: extract cancelled"); return false; }
        if (e.isDirectory) continue;

        fs::path fullPath = fs::path(destPath) / e.name;
        fs::create_directories(fullPath.parent_path());

        if (!Extract(e.name, fullPath.string()))
        {
            LOG_ERR("MpqEngine: failed to extract %s", e.name.c_str());
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> MpqEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen || !m_handle) return {};

    auto* hMpq = static_cast<HANDLE>(m_handle);
    std::string name = toStormPath(entryName);

    HANDLE hFile = nullptr;
    if (!SFileOpenFileEx(hMpq, name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
        return {};

    DWORD fileSize = SFileGetFileSize(hFile, nullptr);
    if (fileSize == SFILE_INVALID_SIZE)
    {
        SFileCloseFile(hFile);
        return {};
    }

    std::vector<uint8_t> data(fileSize);
    DWORD read = 0;
    bool ok = SFileReadFile(hFile, data.data(), fileSize, &read, nullptr);
    SFileCloseFile(hFile);

    if (!ok || read != fileSize)
        return {};

    return data;
}

// ── Writing ────────────────────────────────────────────────────────────

bool MpqEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    std::string apath(archivePath);

    m_pendingAdds.push_back({std::string(srcPath), apath});
    m_modified = true;

    ArchiveEntry placeholder;
    placeholder.name = apath;
    placeholder.path = apath;
    m_entries.push_back(std::move(placeholder));

    return true;
}

bool MpqEngine::RemoveEntry(std::string_view entryName)
{
    if (!m_handle) return false;

    std::string name = toStormPath(entryName);

    if (!SFileRemoveFile(static_cast<HANDLE>(m_handle), name.c_str(), SFILE_OPEN_FROM_MPQ))
    {
        LOG_WARN("MpqEngine: failed to remove %s", name.c_str());
        return false;
    }

    // Remove from our entry cache (entries use forward slashes)
    std::string fwdName(entryName);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->path == fwdName)
        {
            m_entries.erase(it);
            break;
        }
    }

    m_modified = true;
    return true;
}

bool MpqEngine::RenameEntry(std::string_view entryName, std::string_view newName)
{
    if (!m_handle) return false;

    std::string oldName = toStormPath(entryName);
    std::string newNameStr = toStormPath(newName);

    if (!SFileRenameFile(static_cast<HANDLE>(m_handle), oldName.c_str(), newNameStr.c_str()))
    {
        LOG_WARN("MpqEngine: failed to rename %s", oldName.c_str());
        return false;
    }

    // Update entry cache (entries use forward slashes)
    std::string fwdOld(entryName);
    std::string fwdNew(newName);
    for (auto& e : m_entries)
    {
        if (e.path == fwdOld)
        {
            e.name = fwdNew;
            e.path = fwdNew;
            break;
        }
    }

    m_modified = true;
    return true;
}

bool MpqEngine::Save()
{
    if (!m_handle || !m_modified) return true;
    m_saveCancelled = false;

    auto* hMpq = static_cast<HANDLE>(m_handle);

    // Compute total bytes for progress reporting
    uint64_t totalBytes = 0;
    for (const auto& pa : m_pendingAdds)
    {
        std::error_code ec;
        auto sz = fs::file_size(pa.srcPath, ec);
        if (!ec) totalBytes += sz;
    }

    // Process pending additions
    size_t fileIdx = 0;
    uint64_t bytesDone = 0;
    for (const auto& pa : m_pendingAdds)
    {
        if (m_saveCancelled) break;

        if (m_saveProgressCb)
        {
            SaveProgressInfo info;
            info.currentFile = static_cast<int>(fileIdx);
            info.totalFiles = static_cast<int>(m_pendingAdds.size());
            info.bytesProcessed = bytesDone;
            info.totalBytes = totalBytes;
            info.fileName = pa.archivePath;
            m_saveProgressCb(info);
        }

        std::error_code ec;
        auto fileSz = fs::file_size(pa.srcPath, ec);
        bytesDone += ec ? 0 : fileSz;

        std::string stormArchived = toStormPath(pa.archivePath);
        int stormLevel = mpqCompressionType(m_compressionLevel);
        if (!SFileAddFileEx(
                hMpq,
                pa.srcPath.c_str(),
                stormArchived.c_str(),
                MPQ_FILE_COMPRESS | MPQ_FILE_SECTOR_CRC,
                stormLevel,
                stormLevel))
        {
            LOG_WARN("MpqEngine: failed to add %s as %s",
                     pa.srcPath.c_str(), pa.archivePath.c_str());
        }
        fileIdx++;
    }
    m_pendingAdds.clear();

    if (m_saveCancelled)
    {
        LOG_DBG("MpqEngine: save cancelled");
        reloadEntries();
        m_modified = false;
        return false;
    }

    // Compact to reclaim space from removed files
    SFileSetCompactCallback(hMpq, nullptr, nullptr);
    SFileCompactArchive(hMpq, nullptr, false);

    // Flush changes to disk
    SFileFlushArchive(hMpq);

    // Re-open to refresh internal state
    SFileCloseArchive(hMpq);
    m_handle = nullptr;

    HANDLE hNew = nullptr;
    if (!SFileOpenArchive(m_path.c_str(), 0, 0, &hNew))
    {
        LOG_ERR("MpqEngine: failed to re-open after save");
        return false;
    }
    m_handle = hNew;

    m_modified = false;
    reloadEntries();
    return true;
}

// Map 0-9 compression level to StormLib compression types
static int mpqCompressionType(int level)
{
    if (level <= 1) return MPQ_COMPRESSION_HUFFMANN;
    if (level <= 3) return MPQ_COMPRESSION_ZLIB;
    if (level <= 6) return MPQ_COMPRESSION_ZLIB;
    return MPQ_COMPRESSION_BZIP2;
}

// ── Testing ────────────────────────────────────────────────────────────

bool MpqEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_handle) return false;

    auto* hMpq = static_cast<HANDLE>(m_handle);
    int total = static_cast<int>(m_entries.size());

    LOG_DBG("MpqEngine: testing integrity (%d entries)", total);

    // First, verify the archive structure
    DWORD archiveResult = SFileVerifyArchive(hMpq);
    if (archiveResult == ERROR_VERIFY_FAILED)
    {
        LOG_ERR("MpqEngine: archive verification failed");
        return false;
    }

    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(i, total);

        const auto& entry = m_entries[i];

        // Verify file checksums if available
        std::string stormName = toStormPath(entry.name);
        DWORD verifyResult = SFileVerifyFile(hMpq, stormName.c_str(), SFILE_VERIFY_ALL);
        if (verifyResult & VERIFY_FILE_ERROR_MASK)
        {
            if (verifyResult & VERIFY_OPEN_ERROR)
            {
                LOG_ERR("MpqEngine: failed to open %s", entry.name.c_str());
                return false;
            }
            if (verifyResult & VERIFY_FILE_SECTOR_CRC_ERROR)
            {
                LOG_ERR("MpqEngine: sector CRC mismatch for %s", entry.name.c_str());
                return false;
            }
            if (verifyResult & VERIFY_FILE_CHECKSUM_ERROR)
            {
                LOG_ERR("MpqEngine: CRC32 mismatch for %s", entry.name.c_str());
                return false;
            }
            if (verifyResult & VERIFY_FILE_MD5_ERROR)
            {
                LOG_ERR("MpqEngine: MD5 mismatch for %s", entry.name.c_str());
                return false;
            }
        }

        // Also verify we can read the file
        auto data = ReadFile(entry.name);
        if (data.empty() && entry.size > 0)
        {
            LOG_ERR("MpqEngine: failed to read %s during integrity check", entry.name.c_str());
            return false;
        }
    }

    LOG_DBG("MpqEngine: integrity check passed");
    return true;
}
