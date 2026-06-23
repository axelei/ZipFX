#include "Bit7zEngine.h"

#include "Logging.h"

#include <bit7z/bit7zlibrary.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitarchivewriter.hpp>
#include <bit7z/bitfileextractor.hpp>
#include <bit7z/bitformat.hpp>
#include <bit7z/bittypes.hpp>
#include <bit7z/bitcompressionlevel.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>

namespace fs = std::filesystem;

static const char* kDllNames[] = {
    "7z.dll",
    "lib7z.so",
    "7z.so",
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
            for (auto& c : ae.name)
                if (c == '\\') c = '/';

            ae.path = ae.name;
            ae.size = item.size();
            ae.packedSize = item.packSize();
            ae.isDirectory = item.isDir();
            ae.permissions = item.attributes() & 0xFFF;
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

bool Bit7zEngine::Create(std::string_view path)
{
    if (!m_lib) return false;
    Close();
    m_path = path;
    m_isNew = true;
    m_isOpen = true;
    m_pendingAdds.clear();
    LOG_DBG("Bit7zEngine: created %s", m_path.c_str());
    return true;
}

void Bit7zEngine::Close()
{
    m_reader.reset();
    m_isOpen = false;
    m_isNew = false;
    m_entries.clear();
    m_pendingAdds.clear();
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

// ── Writing ────────────────────────────────────────────────────────────
bool Bit7zEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    // Normalize to forward slashes for archive-internal paths
    std::string normPath(archivePath);
    for (auto& c : normPath)
        if (c == '\\') c = '/';

    m_pendingAdds[normPath] = std::string(srcPath);

    ArchiveEntry placeholder;
    placeholder.name = normPath;
    placeholder.path = placeholder.name;
    placeholder.size = fs::file_size(std::string(srcPath));
    m_entries.push_back(std::move(placeholder));

    return true;
}

bool Bit7zEngine::RemoveEntry(std::string_view entryName)
{
    (void)entryName;
    return false;
}

bool Bit7zEngine::Save()
{
    if (!m_isOpen || m_pendingAdds.empty()) return true;

    try
    {
        // Use different writer constructor for new vs existing archives
        auto writer = m_isNew
            ? std::unique_ptr<bit7z::BitArchiveWriter>(
                new bit7z::BitArchiveWriter(*m_lib, bit7z::BitFormat::SevenZip))
            : std::unique_ptr<bit7z::BitArchiveWriter>(
                new bit7z::BitArchiveWriter(*m_lib, m_path, bit7z::BitFormat::SevenZip));

        if (!m_password.empty())
            writer->setPassword(m_password, m_encryptHeaders);

        writer->setCompressionLevel(
            static_cast<bit7z::BitCompressionLevel>(6));

        if (m_volumeSize > 0)
            writer->setVolumeSize(m_volumeSize);

        if (!m_isNew)
            writer->setUpdateMode(bit7z::UpdateMode::Append);

        for (const auto& [archivePath, srcPath] : m_pendingAdds)
            writer->addFile(srcPath, archivePath);

        // Progress callback enables cancellation
        writer->setProgressCallback([this](uint64_t) -> bool {
            return !m_saveCancelled;
        });

        writer->compressTo(m_path);

        // Re-open in read mode
        m_isNew = false;
        m_pendingAdds.clear();
        m_reader = std::make_unique<bit7z::BitArchiveReader>(*m_lib, m_path);
        m_isOpen = true;
        m_entries.clear();
        auto items = m_reader->items();
        m_entries.reserve(items.size());
        for (const auto& item : items)
        {
            ArchiveEntry ae;
            ae.name = item.path();
            // Normalize backslashes that 7z.dll may have inserted
            for (auto& c : ae.name)
                if (c == '\\') c = '/';
            ae.path = ae.name;
            ae.size = item.size();
            ae.packedSize = item.packSize();
            ae.isDirectory = item.isDir();
            ae.permissions = item.attributes() & 0xFFF;
            ae.crc = item.crc();
            m_entries.push_back(std::move(ae));
        }

        LOG_DBG("Bit7zEngine: saved %s (%zu entries)", m_path.c_str(), m_entries.size());
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Bit7zEngine: save failed: %s", e.what());
        return false;
    }
}

// ── Testing ────────────────────────────────────────────────────────────
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
