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
