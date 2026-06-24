#include "MpqEngine.h"

#include "Logging.h"

// Ensure TCHAR = char (StormLib was likely built ANSI, not Unicode)
#undef UNICODE
#undef _UNICODE

#include <StormLib.h>

#include <algorithm>
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

bool MpqEngine::Open(std::string_view path)
{
    Close();

    std::string spath(path);

    HANDLE hMpq = nullptr;
    if (!SFileOpenArchive(spath.c_str(), 0, MPQ_OPEN_READ_ONLY, &hMpq))
    {
        LOG_ERR("MpqEngine: failed to open %s", spath.c_str());
        return false;
    }

    m_handle = hMpq;
    m_path = spath;

    // Find files via wildcard search
    SFILE_FIND_DATA findData = {};
    HANDLE hFind = SFileFindFirstFile(hMpq, "*", &findData, nullptr);
    if (hFind)
    {
        do
        {
            ArchiveEntry ae;
            ae.name = findData.cFileName;
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
                        ArchiveEntry ae;
                        ae.name = current;
                        ae.path = current;
                        m_entries.push_back(std::move(ae));
                        current.clear();
                    }
                }
                else
                    current += c;
            }
            if (!current.empty())
            {
                ArchiveEntry ae;
                ae.name = current;
                ae.path = current;
                m_entries.push_back(std::move(ae));
            }
        }
    }

    // Get sizes for entries found via listfile
    for (auto& e : m_entries)
    {
        HANDLE hFile = nullptr;
        if (SFileOpenFileEx(hMpq, e.name.c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
        {
            e.size = SFileGetFileSize(hFile, nullptr);
            DWORD cmpSize = 0;
            DWORD cmpSizeLen = sizeof(cmpSize);
            if (SFileGetFileInfo(hFile, SFileInfoCompressedSize, &cmpSize, cmpSizeLen, &cmpSizeLen))
                e.packedSize = cmpSize;
            SFileCloseFile(hFile);
        }
    }

    m_isOpen = true;
    LOG_DBG("MpqEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
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
    m_entries.clear();
}

std::vector<ArchiveEntry> MpqEngine::ListContents()
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
    auto data = ReadFile(entryName);
    if (data.empty()) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

bool MpqEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
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

    HANDLE hFile = nullptr;
    if (!SFileOpenFileEx(hMpq, std::string(entryName).c_str(), SFILE_OPEN_FROM_MPQ, &hFile))
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

bool MpqEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    int total = static_cast<int>(m_entries.size());
    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(i, total);
    }
    return true;
}
