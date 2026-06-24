#include "FlatArchiveEngine.h"

#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

FlatArchiveEngine::~FlatArchiveEngine() { Close(); }

void FlatArchiveEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
}

bool FlatArchiveEngine::parse(std::string_view path, const char* formatName,
                              std::function<bool(std::ifstream&, FileEntry&)> readEntry)
{
    m_path = path;
    m_formatName = formatName;

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    FileEntry entry;
    while (readEntry(f, entry))
    {
        m_entries.push_back(entry);
        entry = {};
    }

    if (m_entries.empty() && !f.eof()) return false;

    m_isOpen = true;
    return true;
}

bool FlatArchiveEngine::parse(std::string_view path, const char* formatName,
                              std::function<bool(std::ifstream&, std::vector<FileEntry>&)> readHeader)
{
    m_path = path;
    m_formatName = formatName;

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    if (!readHeader(f, m_entries)) return false;

    m_isOpen = true;
    return true;
}

std::vector<ArchiveEntry> FlatArchiveEngine::ListContents()
{
    std::vector<ArchiveEntry> result;
    result.reserve(m_entries.size());
    for (const auto& e : m_entries)
    {
        ArchiveEntry ae;
        ae.name = e.name;
        ae.path = e.name;
        ae.size = e.size;
        ae.packedSize = e.size;
        ae.isDirectory = false;
        result.push_back(std::move(ae));
    }
    return result;
}

int FlatArchiveEngine::findEntry(std::string_view name) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool FlatArchiveEngine::Extract(std::string_view entryName, std::string_view destPath)
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

bool FlatArchiveEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("FlatArchiveEngine: extract cancelled"); return false; }
        auto data = ReadFile(e.name);
        if (data.empty()) return false;
        fs::path dest = fs::path(destPath) / e.name;
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    return true;
}

std::vector<uint8_t> FlatArchiveEngine::ReadFile(std::string_view entryName)
{
    int idx = findEntry(entryName);
    if (idx < 0) return {};

    const auto& e = m_entries[idx];
    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> data(e.size);
    f.seekg(e.offset);
    f.read(reinterpret_cast<char*>(data.data()), e.size);
    if (!f) return {};
    return data;
}

bool FlatArchiveEngine::Create(std::string_view path)
{
    m_path = path;
    m_formatName = FormatName();
    m_entries.clear();
    // Create empty file via doSave
    std::ofstream f(m_path, std::ios::binary);
    if (!f) return false;
    if (!doSave(f)) return false;
    m_isOpen = true;
    return true;
}

bool FlatArchiveEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    std::ifstream src(std::string(srcPath), std::ios::binary | std::ios::ate);
    if (!src) return false;
    auto sz = src.tellg();
    if (sz < 0) return false;
    src.seekg(0);

    auto& entry = m_entries.emplace_back();
    entry.name = archivePath;
    entry.offset = 0;
    entry.size = static_cast<uint32_t>(sz);
    entry.data.resize(static_cast<size_t>(sz));
    src.read(reinterpret_cast<char*>(entry.data.data()), sz);
    return src.good();
}

bool FlatArchiveEngine::RenameEntry(std::string_view entryName, std::string_view newName)
{
    for (auto& e : m_entries)
    {
        if (e.name == entryName)
        {
            e.name = newName;
            return true;
        }
    }
    return false;
}

bool FlatArchiveEngine::RemoveEntry(std::string_view entryName)
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].name == entryName)
        {
            m_entries.erase(m_entries.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool FlatArchiveEngine::Save()
{
    // Ensure all entries have their data cached
    for (auto& e : m_entries)
    {
        if (e.data.empty() && e.size > 0)
        {
            e.data = ReadFile(e.name);
            if (e.data.empty()) return false;
        }
    }

    std::ofstream f(m_path, std::ios::binary);
    if (!f) return false;
    return doSave(f);
}

bool FlatArchiveEngine::TestIntegrity(
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
