#include "ZipEngine.h"

#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

// libzip flags
static constexpr int ZIP_FLAGS = ZIP_FL_OVERWRITE | ZIP_FL_ENC_GUESS;

int ZipEngine::GetCompressionLevel()
{
    return 6; // default compression (0=store, 9=max)
}

// ── Lifecycle ──────────────────────────────────────────────────────────
ZipEngine::~ZipEngine()
{
    Close();
}

bool ZipEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    int err = 0;
    m_zip = zip_open(m_path.c_str(), 0, &err);
    if (!m_zip)
    {
        LOG_DBG("ZipEngine: failed to open %s (error %d)", m_path.c_str(), err);
        return false;
    }

    m_isNew = false;
    m_modified = false;
    LoadEntries();
    LOG_DBG("ZipEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
    return true;
}

bool ZipEngine::Create(std::string_view path)
{
    Close();
    m_path = path;

    int err = 0;
    m_zip = zip_open(m_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!m_zip)
    {
        LOG_ERR("ZipEngine: failed to create %s (error %d)", m_path.c_str(), err);
        return false;
    }

    m_isNew = true;
    m_modified = false;
    m_entries.clear();
    LOG_DBG("ZipEngine: created %s", m_path.c_str());
    return true;
}

void ZipEngine::Close()
{
    if (m_zip)
    {
        zip_close(m_zip);
        m_zip = nullptr;
    }
    m_entries.clear();
    m_pendingAdds.clear();
    m_modified = false;
}

// ── Entry cache ────────────────────────────────────────────────────────
void ZipEngine::LoadEntries()
{
    m_entries.clear();
    if (!m_zip) return;

    zip_int64_t num = zip_get_num_entries(m_zip, 0);
    if (num < 0) return;

    for (zip_int64_t i = 0; i < num; ++i)
    {
        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(m_zip, i, 0, &st) != 0)
            continue;

        const char* name = zip_get_name(m_zip, i, 0);
        if (!name) continue;

        ArchiveEntry entry;
        entry.name = name;
        entry.path = name;
        entry.size = st.size;
        entry.packedSize = st.comp_size;
        entry.crc = st.crc;
        entry.isDirectory = (name[0] && name[strlen(name) - 1] == '/');

        if (st.mtime > 0)
            entry.modified = std::chrono::system_clock::from_time_t(st.mtime);

        m_entries.push_back(std::move(entry));
    }
}

// ── Reading ────────────────────────────────────────────────────────────
std::vector<ArchiveEntry> ZipEngine::ListContents()
{
    return m_entries;
}

bool ZipEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_zip) { qWarning("Extract: m_zip is null"); return false; }

    std::string name(entryName);
    std::string dp(destPath);

    // Create parent directory
    auto slash = dp.find_last_of("/\\");
    if (slash != std::string::npos)
        fs::create_directories(fs::path(dp.substr(0, slash)));

    zip_int64_t num = zip_get_num_entries(m_zip, 0);
    qDebug("Extract: looking for '%s' in zip with %lld entries", name.c_str(), (long long)num);

    zip_int64_t idx = zip_name_locate(m_zip, name.c_str(), 0);
    if (idx < 0)
    {
        zip_error_t* err = zip_get_error(m_zip);
        qWarning("ZipEngine: entry '%s' not found (zip error %d: %s)",
                 name.c_str(),
                 zip_error_code_zip(err),
                 zip_error_strerror(err));
        return false;
    }

    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat_index(m_zip, idx, 0, &st) != 0)
    {
        qWarning("ZipEngine: zip_stat_index failed for '%s'", name.c_str());
        return false;
    }

    if (st.size == 0 && name.back() == '/') // directory
    {
        fs::create_directories(fs::path(dp));
        return true;
    }

    zip_file_t* zf = zip_fopen_index(m_zip, idx, 0);
    if (!zf)
    {
        qWarning("ZipEngine: zip_fopen_index failed for '%s'", name.c_str());
        return false;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(st.size));
    if (st.size > 0)
    {
        zip_int64_t n = zip_fread(zf, buf.data(), buf.size());
        if (n < 0 || static_cast<zip_uint64_t>(n) != st.size)
        {
            zip_fclose(zf);
            return false;
        }
    }
    zip_fclose(zf);

    std::ofstream out(dp, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    return out.good();
}

bool ZipEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& entry : m_entries)
    {
        if (entry.isDirectory)
        {
            fs::create_directories(fs::path(std::string(destPath) + "/" + entry.name));
            continue;
        }

        if (!Extract(entry.name, std::string(destPath) + "/" + entry.name))
        {
            LOG_WARN("ZipEngine: failed to extract %s", entry.name.c_str());
        }
    }
    return true;
}

std::vector<uint8_t> ZipEngine::ReadFile(std::string_view entryName)
{
    if (!m_zip) return {};

    std::string name(entryName);
    zip_int64_t idx = zip_name_locate(m_zip, name.c_str(), 0);
    if (idx < 0) return {};

    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat_index(m_zip, idx, 0, &st) != 0)
        return {};

    if (st.size == 0) return {};

    zip_file_t* zf = zip_fopen_index(m_zip, idx, 0);
    if (!zf) return {};

    std::vector<uint8_t> data(static_cast<size_t>(st.size));
    zip_int64_t n = zip_fread(zf, data.data(), data.size());
    zip_fclose(zf);

    if (n < 0 || static_cast<zip_uint64_t>(n) != st.size)
        return {};

    return data;
}

// ── Writing ────────────────────────────────────────────────────────────
bool ZipEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
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
    if (!m_zip) return false;

    std::string name(entryName);
    zip_int64_t idx = zip_name_locate(m_zip, name.c_str(), 0);
    if (idx < 0)
    {
        // Also try with trailing slash (directory)
        idx = zip_name_locate(m_zip, (name + "/").c_str(), 0);
        if (idx < 0) return false;
    }

    if (zip_delete(m_zip, idx) != 0)
    {
        LOG_WARN("ZipEngine: failed to delete %s", name.c_str());
        return false;
    }

    m_modified = true;
    return true;
}

bool ZipEngine::Save()
{
    if (!m_zip || !m_modified) return true;

    // Flush pending additions
    for (const auto& pa : m_pendingAdds)
    {
        zip_source_t* src = zip_source_file_create(
            pa.srcPath.c_str(), 0, -1, nullptr);
        if (!src)
        {
            LOG_WARN("ZipEngine: can't create source for %s", pa.srcPath.c_str());
            continue;
        }

        zip_int64_t idx = zip_name_locate(
            m_zip, pa.archivePath.c_str(), 0);
        if (idx >= 0)
        {
            // Replace existing entry
            if (zip_file_replace(m_zip, idx, src, GetCompressionLevel()) != 0)
            {
                LOG_WARN("ZipEngine: can't replace %s", pa.archivePath.c_str());
                zip_source_free(src);
            }
        }
        else
        {
            // Add new entry
            if (zip_file_add(m_zip, pa.archivePath.c_str(),
                             src, GetCompressionLevel()) < 0)
            {
                LOG_WARN("ZipEngine: can't add %s", pa.archivePath.c_str());
                zip_source_free(src);
            }
        }
    }
    m_pendingAdds.clear();

    // Commit changes to disk (libzip handles everything in-place)
    if (zip_close(m_zip) != 0)
    {
        LOG_ERR("ZipEngine: failed to close/commit archive");
        return false;
    }
    m_zip = nullptr;

    // Re-open for further operations
    int err = 0;
    m_zip = zip_open(m_path.c_str(), 0, &err);
    if (!m_zip)
    {
        LOG_ERR("ZipEngine: failed to re-open after save");
        return false;
    }

    m_isNew = false;
    m_modified = false;
    m_entries.clear();
    LoadEntries();
    return true;
}

// ── Testing ────────────────────────────────────────────────────────────
bool ZipEngine::TestIntegrity()
{
    if (!m_zip) return false;

    zip_int64_t num = zip_get_num_entries(m_zip, 0);
    LOG_DBG("ZipEngine: testing integrity (%lld entries)", (long long)num);

    for (zip_int64_t i = 0; i < num; ++i)
    {
        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(m_zip, i, 0, &st) != 0)
            return false;

        if (st.size == 0) continue;

        zip_file_t* zf = zip_fopen_index(m_zip, i, 0);
        if (!zf) return false;

        std::vector<uint8_t> buf(8192);
        while (zip_fread(zf, buf.data(), buf.size()) > 0) {}

        zip_fclose(zf);
    }

    LOG_DBG("ZipEngine: integrity check passed");
    return true;
}
