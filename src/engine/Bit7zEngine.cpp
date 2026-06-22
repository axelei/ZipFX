#include "Bit7zEngine.h"

#include "Logging.h"

#include <bit7z/bit7zlibrary.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitfileextractor.hpp>
#include <bit7z/bitformat.hpp>
#include <bit7z/bittypes.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>

namespace fs = std::filesystem;

static const char* kDllNames[] = {
    "7z.dll",
    "lib7z.so",
    "lib7z.dylib",
};

Bit7zEngine::Bit7zEngine()
{
    for (auto name : kDllNames)
    {
        try
        {
            m_lib = std::make_unique<bit7z::Bit7zLibrary>(name);
            LOG_DBG("Bit7zEngine: loaded %s", name);
            break;
        }
        catch (...)
        {
            continue;
        }
    }
    if (!m_lib)
        LOG_WARN("Bit7zEngine: no 7-Zip library found");
}

Bit7zEngine::~Bit7zEngine()
{
    Close();
}

bool Bit7zEngine::Open(std::string_view path)
{
    if (!m_lib) return false;
    Close();

    try
    {
        auto pathStr = std::string(path);
        m_reader = std::make_unique<bit7z::BitArchiveReader>(*m_lib, pathStr);

        m_path = pathStr;
        m_isOpen = true;

        auto items = m_reader->items();
        m_entries.reserve(items.size());

        for (const auto& item : items)
        {
            ArchiveEntry ae;
            ae.name = item.path();
            if (ae.name.empty()) continue;

            ae.path = ae.name;
            ae.size = item.size();
            ae.packedSize = item.packSize();
            ae.isDirectory = item.isDir();
            ae.crc = item.crc();
            m_entries.push_back(std::move(ae));
        }

        LOG_DBG("Bit7zEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Bit7zEngine: failed to open %s: %s", std::string(path).c_str(), e.what());
        Close();
        return false;
    }
}

void Bit7zEngine::Close()
{
    m_reader.reset();
    m_isOpen = false;
    m_entries.clear();
}

std::vector<ArchiveEntry> Bit7zEngine::ListContents()
{
    return m_entries;
}

int Bit7zEngine::findEntry(std::string_view name) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].path == name)
            return static_cast<int>(i);
    return -1;
}

bool Bit7zEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;

    int idx = findEntry(entryName);
    if (idx < 0) return false;

    try
    {
        bit7z::BitFileExtractor extractor(*m_lib);
        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());

        // Extract to buffer then write to disk
        std::vector<bit7z::byte_t> buf;
        extractor.extract(m_path, buf, static_cast<uint32_t>(idx));

        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        return out.good();
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Bit7zEngine: extract failed: %s", e.what());
        return false;
    }
}

bool Bit7zEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;

    try
    {
        bit7z::BitFileExtractor extractor(*m_lib);
        extractor.extract(m_path, std::string(destPath));
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Bit7zEngine: extract all failed: %s", e.what());
        return false;
    }
}

std::vector<uint8_t> Bit7zEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};

    int idx = findEntry(entryName);
    if (idx < 0) return {};

    try
    {
        bit7z::BitFileExtractor extractor(*m_lib);
        std::vector<bit7z::byte_t> buf;
        extractor.extract(m_path, buf, static_cast<uint32_t>(idx));

        std::vector<uint8_t> data(buf.begin(), buf.end());
        return data;
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Bit7zEngine: read file failed: %s", e.what());
        return {};
    }
}

bool Bit7zEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;

    int total = static_cast<int>(m_entries.size());

    try
    {
        for (int i = 0; i < total; ++i)
        {
            if (cancelFlag && cancelFlag()) return false;
            if (progressCallback) progressCallback(i, total);
        }
        bit7z::BitFileExtractor extractor(*m_lib);
        extractor.test(m_path);
        if (progressCallback) progressCallback(total, total);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
