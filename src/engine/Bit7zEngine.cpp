#include "Bit7zEngine.h"

#include "Logging.h"

#include <bit7z/bit7zlibrary.hpp>
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitarchiveeditor.hpp>
#include <bit7z/bitarchivewriter.hpp>
#include <bit7z/bitfileextractor.hpp>
#include <bit7z/bitformat.hpp>
#include <bit7z/bittypes.hpp>
#include <bit7z/bitcompressionlevel.hpp>
#include <bit7z/bitpropvariant.hpp>

#include <QApplication>
#include <QDir>

#include <filesystem>
#include <fstream>
#include <functional>
#include <cstring>

namespace fs = std::filesystem;

static const char* kDllNames[] = {
    "7z.dll",
    "lib7z.dylib",
    "lib7z.so",
    "lib7z.arm64.dylib",
    "7z.so",
};

Bit7zEngine::Bit7zEngine()
{
    // Build search paths
    QStringList searchPaths;
    searchPaths << QApplication::applicationDirPath()
                << QApplication::applicationDirPath() + "/../Resources"
                << QApplication::applicationDirPath() + "/../PlugIns";

#ifdef __APPLE__
    // Development paths: look next to the source tree
    {
        QString base = QApplication::applicationDirPath();
        for (int i = 0; i < 4; i++) {
            base += "/..";
            QString libPath = base + "/lib/macos/arm64/";
            if (QDir(libPath).exists())
                searchPaths << QDir(libPath).absolutePath();
        }
    }
    searchPaths << "/usr/local/lib" << "/opt/homebrew/lib";
#endif

    for (auto name : kDllNames)
    {
        for (const auto& dir : searchPaths)
        {
            QString fullPath = dir + "/" + name;
            try
            {
                m_lib = std::make_unique<bit7z::Bit7zLibrary>(fullPath.toStdString());
                LOG_DBG("Bit7zEngine: loaded %s", qPrintable(fullPath));
                break;
            }
            catch (...)
            {
                continue;
            }
        }
        if (m_lib) break;
    }

    // Also try bare names (system search)
    if (!m_lib)
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
            if (ae.permissions == 0)
                ae.permissions = ae.isDirectory ? 0755 : 0644;
            ae.crc = item.crc();
            try {
                auto cv = item.itemProperty(bit7z::BitProperty::Comment);
                if (cv.isString()) ae.comment = cv.getString();
            } catch (...) {}
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
    m_modified = false;
    m_entries.clear();
    m_pendingAdds.clear();
    m_pendingDeletes.clear();
}

std::string Bit7zEngine::archiveComment() const
{
    if (!m_reader) return {};
    try {
        auto val = m_reader->archiveProperty(bit7z::BitProperty::Comment);
        if (val.isString()) return val.getString();
    } catch (...) {}
    return {};
}

const std::vector<ArchiveEntry>& Bit7zEngine::ListContents()
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

    // Reset cancel flag at the start of extraction
    m_extractCancelled = false;

    // Determine total bytes for progress reporting
    uint64_t totalBytes = 0;
    if (idx >= 0 && idx < static_cast<int>(m_entries.size()))
        totalBytes = m_entries[idx].packedSize > 0 ? m_entries[idx].packedSize : m_entries[idx].size;

    try
    {
        bit7z::BitFileExtractor extractor(*m_lib);

        // Progress callback keeps UI responsive, reports ETA, enables cancellation
        extractor.setProgressCallback([this, entryName, totalBytes](uint64_t processed) -> bool {
            if (m_extractProgressCb)
            {
                ExtractProgressInfo info;
                info.bytesProcessed = processed;
                info.totalBytes = totalBytes;
                info.fileName = entryName;
                m_extractProgressCb(info);
            }
            return !m_extractCancelled;
        });

        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());

        // Stream directly to file (avoids loading entire file into memory)
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        extractor.extract(m_path, out, static_cast<uint32_t>(idx));
        return out.good();
    }
    catch (const std::exception& e)
    {
        if (m_extractCancelled)
        {
            std::error_code ec;
            fs::remove(std::string(destPath), ec);
        }
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

    // Determine expected size for validation
    uint64_t expectedSize = 0;
    if (idx >= 0 && idx < static_cast<int>(m_entries.size()))
        expectedSize = m_entries[idx].size;

    try
    {
        bit7z::BitFileExtractor extractor(*m_lib);

        // Report progress for UI responsiveness
        extractor.setProgressCallback([this](uint64_t) -> bool {
            return !m_extractCancelled;
        });

        std::vector<bit7z::byte_t> buf;
        extractor.extract(m_path, buf, static_cast<uint32_t>(idx));

        std::vector<uint8_t> data(buf.begin(), buf.end());

        // If the extracted data is smaller than expected and we're not cancelled,
        // the extraction likely hit a volume boundary — log a warning.
        if (!data.empty() && expectedSize > 0 && data.size() < expectedSize
            && !m_extractCancelled)
        {
            LOG_WARN("Bit7zEngine: read %s truncated: got %zu bytes, expected %llu",
                     std::string(entryName).c_str(), data.size(),
                     static_cast<unsigned long long>(expectedSize));
        }

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
    std::string name(entryName);
    for (auto& c : name)
        if (c == '\\') c = '/';

    m_pendingDeletes.insert(name);
    m_modified = true;

    std::erase_if(m_entries, [&](const ArchiveEntry& e) { return e.path == name; });
    return true;
}

bool Bit7zEngine::Save()
{
    if (!m_isOpen || (m_pendingAdds.empty() && m_pendingDeletes.empty())) return true;

    try
    {
        bool hasDeletes = !m_pendingDeletes.empty() && !m_isNew;

        if (hasDeletes)
        {
            auto editor = std::make_unique<bit7z::BitArchiveEditor>(
                *m_lib, m_path, bit7z::BitFormat::SevenZip);

            if (!m_password.empty())
                editor->setPassword(m_password, m_encryptHeaders);

            editor->setCompressionLevel(
                static_cast<bit7z::BitCompressionLevel>(m_compressionLevel));

            for (const auto& name : m_pendingDeletes)
                editor->deleteItem(name);

            for (const auto& [archivePath, srcPath] : m_pendingAdds)
                editor->addFile(srcPath, archivePath);

            editor->applyChanges();
        }
        else
        {
            auto writer = m_isNew
                ? std::make_unique<bit7z::BitArchiveWriter>(*m_lib, bit7z::BitFormat::SevenZip)
                : std::make_unique<bit7z::BitArchiveWriter>(*m_lib, m_path, bit7z::BitFormat::SevenZip);

            if (!m_password.empty())
                writer->setPassword(m_password, m_encryptHeaders);

            writer->setCompressionLevel(
                static_cast<bit7z::BitCompressionLevel>(m_compressionLevel));

            if (m_volumeSize > 0)
                writer->setVolumeSize(m_volumeSize);

            if (!m_isNew)
                writer->setUpdateMode(bit7z::UpdateMode::Append);

            // Compute total bytes
            uint64_t totalBytes7z = 0;
            for (const auto& [archivePath, srcPath] : m_pendingAdds)
            {
                std::error_code ec;
                totalBytes7z += fs::file_size(srcPath, ec);
            }

            for (const auto& [archivePath, srcPath] : m_pendingAdds)
                writer->addFile(srcPath, archivePath);

            writer->setProgressCallback([this, totalBytes7z](uint64_t processed) -> bool {
                if (m_saveProgressCb)
                {
                    SaveProgressInfo info;
                    info.bytesProcessed = processed;
                    info.totalBytes = totalBytes7z;
                    m_saveProgressCb(info);
                }
                return !m_saveCancelled;
            });

            writer->compressTo(m_path);
        }

        // Re-open in read mode
        m_isNew = false;
        m_pendingAdds.clear();
        m_pendingDeletes.clear();
        m_modified = false;
        m_reader = std::make_unique<bit7z::BitArchiveReader>(*m_lib, m_path);
        m_isOpen = true;
        m_entries.clear();
        auto items = m_reader->items();
        m_entries.reserve(items.size());
        for (const auto& item : items)
        {
            ArchiveEntry ae;
            ae.name = item.path();
            for (auto& c : ae.name)
                if (c == '\\') c = '/';
            ae.path = ae.name;
            ae.size = item.size();
            ae.packedSize = item.packSize();
            ae.isDirectory = item.isDir();
            ae.permissions = item.attributes() & 0xFFF;
            if (ae.permissions == 0)
                ae.permissions = ae.isDirectory ? 0755 : 0644;
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
