#include "ZipEngine.h"

#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

// libzip flags
static constexpr int ZIP_FLAGS = ZIP_FL_OVERWRITE | ZIP_FL_ENC_GUESS;

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
        // permission bits from zip_file_attributes
        {
            zip_uint8_t opsys;
            zip_uint32_t attrs;
            if (zip_file_get_external_attributes(m_zip, i, 0, &opsys, &attrs) == 0 && opsys == 3) // Unix
                entry.permissions = (attrs >> 16) & 0xFFF;
            else
                entry.permissions = entry.isDirectory ? 0755 : 0644;
        }

        if (st.mtime > 0)
            entry.modified = std::chrono::system_clock::from_time_t(st.mtime);

        // Read per-file comment
        zip_uint32_t commentLen = 0;
        const char* comment = zip_file_get_comment(m_zip, i, &commentLen, 0);
        if (comment && commentLen > 0)
            entry.comment = std::string(comment, commentLen);

        m_entries.push_back(std::move(entry));
    }
}

// ── Reading ────────────────────────────────────────────────────────────
const std::vector<ArchiveEntry>& ZipEngine::ListContents()
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

    zip_file_t* zf = nullptr;
    if (st.encryption_method != ZIP_EM_NONE && !m_password.empty())
        zf = zip_fopen_index_encrypted(m_zip, idx, 0, m_password.c_str());
    else
        zf = zip_fopen_index(m_zip, idx, 0);
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
        if (m_extractCancelled) { LOG_DBG("ZipEngine: extract cancelled"); return false; }
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

    zip_file_t* zf = nullptr;
    if (st.encryption_method != ZIP_EM_NONE && !m_password.empty())
        zf = zip_fopen_index_encrypted(m_zip, idx, 0, m_password.c_str());
    else
        zf = zip_fopen_index(m_zip, idx, 0);
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

bool ZipEngine::RenameEntry(std::string_view entryName, std::string_view newName)
{
    if (!m_zip) return false;

    std::string name(entryName);
    zip_int64_t idx = zip_name_locate(m_zip, name.c_str(), 0);
    if (idx < 0)
    {
        idx = zip_name_locate(m_zip, (name + "/").c_str(), 0);
        if (idx < 0) return false;
    }

    if (zip_file_rename(m_zip, idx, std::string(newName).c_str(), 0) != 0)
    {
        LOG_WARN("ZipEngine: failed to rename %s", name.c_str());
        return false;
    }

    m_modified = true;
    return true;
}

bool ZipEngine::Save()
{
    if (!m_zip || !m_modified) return true;
    m_saveCancelled = false;

    // Compute total bytes for progress reporting
    uint64_t totalBytes = 0;
    for (const auto& pa : m_pendingAdds)
    {
        std::error_code ec;
        auto sz = fs::file_size(pa.srcPath, ec);
        if (!ec) totalBytes += sz;
    }

    // Flush pending additions
    std::string lastName;
    for (const auto& pa : m_pendingAdds)
    {
        if (m_saveCancelled) break;

        lastName = pa.archivePath;

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
            if (zip_file_replace(m_zip, idx, src, ZIP_FL_OVERWRITE) != 0)
            {
                LOG_WARN("ZipEngine: can't replace %s", pa.archivePath.c_str());
                zip_source_free(src);
            }
        }
        else
        {
            idx = zip_file_add(m_zip, pa.archivePath.c_str(), src, 0);
            if (idx < 0)
            {
                LOG_WARN("ZipEngine: can't add %s", pa.archivePath.c_str());
                zip_source_free(src);
            }
        }

        if (idx >= 0)
        {
            zip_int32_t method = (m_compressionLevel == 0) ? ZIP_CM_STORE : ZIP_CM_DEFLATE;
            zip_set_file_compression(m_zip, idx, method,
                                     static_cast<zip_uint32_t>(m_compressionLevel));
        }

        if (idx >= 0 && !m_password.empty())
        {
            zip_uint16_t method = m_encryptHeaders ? ZIP_EM_AES_256 : ZIP_EM_AES_128;
            zip_file_set_encryption(m_zip, idx, method, m_password.c_str());
        }

        if (idx >= 0)
        {
            fs::perms perms = fs::status(pa.srcPath).permissions();
            zip_uint32_t attrs = (static_cast<zip_uint32_t>(perms) & 0xFFF) << 16;
            zip_file_set_external_attributes(m_zip, idx, 0, 3, attrs);
        }
    }
    m_pendingAdds.clear();

    if (m_saveCancelled)
    {
        zip_discard(m_zip);
        m_zip = nullptr;
        LOG_DBG("ZipEngine: save cancelled, discarding changes");

        int err = 0;
        m_zip = zip_open(m_path.c_str(), 0, &err);
        m_entries.clear();
        if (m_zip)
            LoadEntries();
        m_modified = false;
        return false;
    }

    // Register progress callback for zip_close (where actual compression happens)
    if (m_saveProgressCb && totalBytes > 0)
    {
        zip_register_progress_callback_with_state(m_zip, 0.001,
            [](zip_t*, double progress, void* ud) {
                auto* self = static_cast<ZipEngine*>(ud);
                if (self->m_saveProgressCb)
                {
                    SaveProgressInfo info;
                    info.bytesProcessed = static_cast<uint64_t>(
                        progress * static_cast<double>(self->m_totalBytesForProgress));
                    info.totalBytes = self->m_totalBytesForProgress;
                    info.fileName = self->m_lastFileName;
                    self->m_saveProgressCb(info);
                }
            },
            nullptr, this);
        m_totalBytesForProgress = totalBytes;
        m_lastFileName = lastName;
    }

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
bool ZipEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_zip) return false;

    zip_int64_t num = zip_get_num_entries(m_zip, 0);
    LOG_DBG("ZipEngine: testing integrity (%lld entries)", (long long)num);

    for (zip_int64_t i = 0; i < num; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback((int)i, (int)num);

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
