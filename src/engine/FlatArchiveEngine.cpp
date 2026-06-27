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

void FlatArchiveEngine::rebuildArchiveEntries()
{
    m_archiveEntries.clear();
    m_archiveEntries.reserve(m_entries.size());
    for (const auto& e : m_entries)
    {
        ArchiveEntry ae;
        ae.name = e.name;
        ae.path = e.name;
        ae.size = e.size;
        ae.packedSize = e.size;
        ae.isDirectory = false;
        m_archiveEntries.push_back(std::move(ae));
    }
}

const std::vector<ArchiveEntry>& FlatArchiveEngine::ListContents()
{
    rebuildArchiveEntries();
    return m_archiveEntries;
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
    int idx = findEntry(entryName);
    if (idx < 0) return false;

    const auto& e = m_entries[idx];
    std::ifstream in(m_path, std::ios::binary);
    if (!in) return false;
    in.seekg(e.offset);

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;

    m_extractCancelled = false;
    constexpr size_t kChunk = 65536;
    char buf[kChunk];
    uint32_t remaining = e.size;
    while (remaining > 0)
    {
        if (m_extractCancelled) return false;
        size_t toRead = std::min<size_t>(remaining, kChunk);
        in.read(buf, toRead);
        if (!in) return false;
        out.write(buf, toRead);
        remaining -= static_cast<uint32_t>(toRead);
    }
    return out.good();
}

bool FlatArchiveEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("FlatArchiveEngine: extract cancelled"); return false; }

        fs::path dest = fs::path(destPath) / e.name;
        fs::create_directories(dest.parent_path());

        if (!Extract(e.name, dest.string()))
            return false;
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

std::vector<uint8_t> FlatArchiveEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    int idx = findEntry(entryName);
    if (idx < 0) return {};

    const auto& e = m_entries[idx];
    if (e.size == 0) return {};

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};

    size_t readSize = std::min(static_cast<size_t>(e.size), maxBytes);
    std::vector<uint8_t> data(readSize);
    f.seekg(e.offset);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(readSize));
    auto got = static_cast<size_t>(f.gcount());
    if (got == 0) return {};
    data.resize(got);
    return data;
}

bool FlatArchiveEngine::ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer)
{
    int idx = findEntry(entryName);
    if (idx < 0) return false;

    const auto& e = m_entries[idx];
    if (e.size == 0) return true;

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;
    f.seekg(e.offset);

    constexpr size_t kChunk = 65536;
    std::array<uint8_t, kChunk> buf;
    uint32_t remaining = e.size;
    while (remaining > 0)
    {
        size_t toRead = std::min<uint32_t>(remaining, kChunk);
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(toRead));
        auto got = static_cast<size_t>(f.gcount());
        if (got == 0) return false;
        if (!consumer(buf.data(), got)) return false;
        remaining -= static_cast<uint32_t>(got);
    }
    return true;
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
    uint64_t totalBytes = 0;
    for (const auto& e : m_entries)
        totalBytes += e.size;
    uint64_t bytesDone = 0;
    int fileIdx = 0;
    int totalFiles = static_cast<int>(m_entries.size());

    // Ensure all entries have their data cached
    for (auto& e : m_entries)
    {
        if (m_saveProgressCb)
        {
            SaveProgressInfo info;
            info.currentFile = fileIdx;
            info.totalFiles = totalFiles;
            info.bytesProcessed = bytesDone;
            info.totalBytes = totalBytes;
            info.fileName = e.name;
            m_saveProgressCb(info);
        }

        if (e.data.empty() && e.size > 0)
        {
            e.data = ReadFile(e.name);
            if (e.data.empty()) return false;
        }
        bytesDone += e.size;
        fileIdx++;
    }

    if (m_saveCancelled) return false;

    std::string tmpPath = m_path + ".zipfx_tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary);
        if (!f) return false;
        if (!doSave(f)) { fs::remove(tmpPath); return false; }
    }

    std::error_code ec;
    fs::rename(tmpPath, m_path, ec);
    if (ec)
    {
        LOG_ERR("FlatArchiveEngine: failed to rename temp file: %s", ec.message().c_str());
        fs::remove(tmpPath);
        return false;
    }
    return true;
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
