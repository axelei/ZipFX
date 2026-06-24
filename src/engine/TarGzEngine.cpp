#include "TarGzEngine.h"

#include "Logging.h"

#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#endif
#include <vector>

namespace fs = std::filesystem;

// ── Tar constants ──────────────────────────────────────────────────────
static constexpr size_t TAR_BLOCK_SIZE = 512;
static constexpr size_t TAR_NAME_SIZE  = 100;
static constexpr size_t TAR_PREFIX_SIZE = 155;

struct TarHeader
{
    char name[TAR_NAME_SIZE];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[TAR_NAME_SIZE];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[TAR_PREFIX_SIZE];
    char padding[12];
};

static_assert(sizeof(TarHeader) == TAR_BLOCK_SIZE, "tar header must be 512 bytes");

static bool IsEndOfArchive(const TarHeader* hdr)
{
    const char* p = reinterpret_cast<const char*>(hdr);
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i)
    {
        if (p[i] != 0) return false;
    }
    return true;
}

static uint64_t ParseOctal(const char* data, size_t len)
{
    uint64_t value = 0;
    for (size_t i = 0; i < len && data[i]; ++i)
    {
        if (data[i] >= '0' && data[i] <= '7')
        {
            value = (value << 3) | static_cast<uint64_t>(data[i] - '0');
        }
    }
    return value;
}

static void FormatOctal(char* buf, size_t len, uint64_t value)
{
    buf[len - 1] = '\0';
    for (size_t i = len - 1; i > 0; --i)
    {
        buf[i - 1] = static_cast<char>('0' + (value & 7));
        value >>= 3;
    }
}

static bool ValidateChecksum(const TarHeader* hdr)
{
    uint64_t stored = ParseOctal(hdr->chksum, 8);

    uint64_t computed = 0;
    const char* p = reinterpret_cast<const char*>(hdr);
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i)
    {
        if (i >= 148 && i < 156)
        {
            computed += ' ';
        }
        else
        {
            computed += static_cast<unsigned char>(p[i]);
        }
    }

    return stored == computed;
}

static uint64_t ComputeChecksum(const TarHeader* hdr)
{
    uint64_t computed = 0;
    const char* p = reinterpret_cast<const char*>(hdr);
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i)
    {
        if (i >= 148 && i < 156)
        {
            computed += ' ';
        }
        else
        {
            computed += static_cast<unsigned char>(p[i]);
        }
    }
    return computed;
}

static std::string TarName(const TarHeader* hdr)
{
    std::string result;
    if (hdr->prefix[0])
    {
        result.append(hdr->prefix, strnlen(hdr->prefix, TAR_PREFIX_SIZE));
        result.push_back('/');
    }
    result.append(hdr->name, strnlen(hdr->name, TAR_NAME_SIZE));
    return result;
}

static std::string StripDotSlash(const std::string& name)
{
    if (name.size() >= 2 && name[0] == '.' && name[1] == '/')
        return name.substr(2);
    return name;
}

static std::string TarLinkName(const TarHeader* hdr)
{
    std::string result(hdr->linkname, strnlen(hdr->linkname, TAR_NAME_SIZE));
    return StripDotSlash(result);
}

// ── Lifecycle ──────────────────────────────────────────────────────────
TarGzEngine::~TarGzEngine()
{
    TarGzEngine::Close();
}

bool TarGzEngine::Create(std::string_view path)
{
    Close();
    m_path = path;
    m_isOpen = true;
    m_modified = false;
    m_entries.clear();
    return true;
}

// Skip fileSize bytes (padded to TAR_BLOCK_SIZE) in the gzFile stream
static void SkipTarData(gzFile f, uint64_t fileSize)
{
    uint64_t skipBytes = ((fileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
    std::vector<char> skipBuf(static_cast<size_t>(
        std::min(skipBytes, static_cast<uint64_t>(65536))));
    while (skipBytes > 0)
    {
        size_t toRead = static_cast<size_t>(
            std::min(skipBytes, static_cast<uint64_t>(skipBuf.size())));
        int r = gzread(f, skipBuf.data(), static_cast<unsigned int>(toRead));
        if (r <= 0) break;
        skipBytes -= static_cast<uint64_t>(r);
    }
}

// Read fileSize bytes from the gzFile stream (the data block following a header)
static std::string ReadTarDataAsString(gzFile f, uint64_t fileSize)
{
    std::string result(static_cast<size_t>(fileSize), '\0');
    if (fileSize > 0)
        gzread(f, result.data(), static_cast<unsigned int>(fileSize));
    // Skip remaining padding
    uint64_t padded = ((fileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;
    uint64_t remaining = padded - fileSize;
    if (remaining > 0)
    {
        std::vector<char> pad(static_cast<size_t>(remaining));
        gzread(f, pad.data(), static_cast<unsigned int>(remaining));
    }
    // Trim trailing nulls (GNU long names are null-terminated)
    while (!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}

bool TarGzEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    gzFile f = gzopen(m_path.c_str(), "rb");
    if (!f)
    {
        return false;
    }

    m_gzFile = f;
    m_isOpen = true;
    m_modified = false;
    m_entries.clear();
    m_linkTargets.clear();

    LOG_DBG("TarGzEngine: reading entries from %s", m_path.c_str());

    std::string gnuLongName;
    std::string gnuLongLink;

    TarHeader hdr;
    while (true)
    {
        int bytesRead = gzread(f, &hdr, TAR_BLOCK_SIZE);
        if (bytesRead < static_cast<int>(TAR_BLOCK_SIZE))
            break;

        if (IsEndOfArchive(&hdr))
            break;

        if (!ValidateChecksum(&hdr))
            break;

        uint64_t fileSize = ParseOctal(hdr.size, 12);

        // GNU long name (typeflag 'L'): the data block contains the real filename
        if (hdr.typeflag == 'L')
        {
            gnuLongName = ReadTarDataAsString(f, fileSize);
            continue;
        }

        // GNU long link (typeflag 'K'): the data block contains the real link target
        if (hdr.typeflag == 'K')
        {
            gnuLongLink = ReadTarDataAsString(f, fileSize);
            continue;
        }

        // PAX extended header (typeflag 'x' or 'g'): skip for now
        if (hdr.typeflag == 'x' || hdr.typeflag == 'g')
        {
            SkipTarData(f, fileSize);
            continue;
        }

        std::string entryName = gnuLongName.empty()
            ? TarName(&hdr) : gnuLongName;
        entryName = StripDotSlash(entryName);
        gnuLongName.clear();

        std::string linkTarget = gnuLongLink.empty()
            ? TarLinkName(&hdr) : gnuLongLink;
        gnuLongLink.clear();

        bool isHardlink = (hdr.typeflag == '1');
        bool isSymlink = (hdr.typeflag == '2');
        bool isDir = (hdr.typeflag == '5');

        // Track hardlink targets so ReadFile can resolve them
        if (isHardlink && !linkTarget.empty())
            m_linkTargets[entryName] = linkTarget;

        // For hardlinks, resolve the size from the link target
        uint64_t displaySize = fileSize;
        if (isHardlink && fileSize == 0 && !linkTarget.empty())
        {
            for (const auto& prev : m_entries)
                if (prev.name == linkTarget)
                    { displaySize = prev.size; break; }
        }

        ArchiveEntry entry;
        entry.name = entryName;
        entry.path = entryName;
        entry.size = displaySize;
        entry.packedSize = displaySize;
        entry.isDirectory = isDir;
        entry.permissions = ParseOctal(hdr.mode, 8) & 0xFFF;

        if (isSymlink)
            entry.comment = "-> " + linkTarget;

        uint64_t mtime = ParseOctal(hdr.mtime, 12);
        entry.modified = std::chrono::system_clock::from_time_t(
            static_cast<time_t>(mtime));

        m_entries.push_back(std::move(entry));

        SkipTarData(f, fileSize);
    }

    LOG_DBG("TarGzEngine: loaded %zu entries", m_entries.size());
    return true;
}

void TarGzEngine::Close()
{
    if (m_gzFile)
    {
        gzclose(m_gzFile);
        m_gzFile = nullptr;
    }
    m_isOpen = false;
    m_modified = false;
    m_entries.clear();
    m_entryQueue.clear();
    m_removedEntries.clear();
    m_linkTargets.clear();
}

// ── Reading ────────────────────────────────────────────────────────────
const std::vector<ArchiveEntry>& TarGzEngine::ListContents()
{
    return m_entries;
}

bool TarGzEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& entry : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("TarGzEngine: extract cancelled"); return false; }
        if (entry.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / entry.name);
            continue;
        }

        fs::path fullPath = fs::path(destPath) / entry.name;
        fs::create_directories(fullPath.parent_path());

        if (!Extract(entry.name, fullPath.string()))
        {
            return false;
        }
    }

    return true;
}

std::vector<uint8_t> TarGzEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen)
        return {};

    // Resolve hardlinks: if this entry is a hardlink, read from the target
    std::string name(entryName);
    auto linkIt = m_linkTargets.find(name);
    if (linkIt != m_linkTargets.end())
        name = linkIt->second;

    gzclose(m_gzFile);
    m_gzFile = gzopen(m_path.c_str(), "rb");
    if (!m_gzFile)
        return {};

    std::string gnuLongName;

    TarHeader hdr;
    while (true)
    {
        int bytesRead = gzread(m_gzFile, &hdr, TAR_BLOCK_SIZE);
        if (bytesRead < static_cast<int>(TAR_BLOCK_SIZE))
            break;

        if (IsEndOfArchive(&hdr))
            break;

        if (!ValidateChecksum(&hdr))
            break;

        uint64_t fileSize = ParseOctal(hdr.size, 12);

        // Handle GNU long name
        if (hdr.typeflag == 'L')
        {
            gnuLongName = ReadTarDataAsString(m_gzFile, fileSize);
            continue;
        }

        // Skip PAX headers and GNU long link
        if (hdr.typeflag == 'x' || hdr.typeflag == 'g' || hdr.typeflag == 'K')
        {
            SkipTarData(m_gzFile, fileSize);
            continue;
        }

        std::string currentName = gnuLongName.empty()
            ? TarName(&hdr) : gnuLongName;
        currentName = StripDotSlash(currentName);
        gnuLongName.clear();

        if (currentName == name && fileSize > 0)
        {
            std::vector<uint8_t> result(static_cast<size_t>(fileSize));
            int r = gzread(m_gzFile, result.data(),
                           static_cast<unsigned int>(fileSize));
            if (r <= 0)
                return {};
            return result;
        }

        SkipTarData(m_gzFile, fileSize);
    }

    return {};
}

// ── Writing ────────────────────────────────────────────────────────────
bool TarGzEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    m_modified = true;
    m_entryQueue.push_back(QueueEntry{std::string(srcPath), std::string(archivePath)});
    return true;
}

bool TarGzEngine::RemoveEntry(std::string_view entryName)
{
    m_removedEntries.insert(std::string(entryName));
    m_modified = true;
    return true;
}

bool TarGzEngine::Save()
{
    if (!m_modified && m_entryQueue.empty())
    {
        return true;
    }
    m_saveCancelled = false;

    LOG_DBG("TarGzEngine: saving %s (%zu existing, %zu queued, %zu removed)",
            m_path.c_str(), m_entries.size(), m_entryQueue.size(), m_removedEntries.size());

    // Collect existing entries' data before overwriting the file
    struct MergedEntry
    {
        std::string archivePath;
        std::vector<uint8_t> data;
        uint32_t permissions;
        uint64_t mtime;
    };
    std::vector<MergedEntry> merged;

    // Names of new entries (so we skip originals being replaced)
    std::set<std::string> newNames;
    for (const auto& qe : m_entryQueue)
        newNames.insert(qe.archivePath);

    // Preserve existing entries that are not removed or replaced
    for (const auto& entry : m_entries)
    {
        if (entry.isDirectory) continue;
        if (m_removedEntries.count(entry.name)) continue;
        if (newNames.count(entry.name)) continue;

        auto data = ReadFile(entry.name);
        MergedEntry me;
        me.archivePath = entry.name;
        me.data = std::move(data);
        me.permissions = entry.permissions;
        me.mtime = static_cast<uint64_t>(
            std::chrono::system_clock::to_time_t(entry.modified));
        merged.push_back(std::move(me));
    }

    // Compute total bytes for progress
    uint64_t totalTarBytes = 0;
    int totalTarFiles = static_cast<int>(merged.size());
    for (const auto& me : merged)
        totalTarBytes += me.data.size();
    for (const auto& qe : m_entryQueue)
    {
        std::error_code ec;
        auto sz = fs::file_size(qe.srcPath, ec);
        if (!ec) totalTarBytes += sz;
        totalTarFiles++;
    }

    gzFile out = gzopen(m_path.c_str(), "wb");
    if (!out)
    {
        LOG_ERR("TarGzEngine: cannot write %s", m_path.c_str());
        return false;
    }

    auto writeBlock = [&](const void* data, size_t size)
    {
        gzwrite(out, data, static_cast<unsigned int>(size));
        size_t pad = (TAR_BLOCK_SIZE - size % TAR_BLOCK_SIZE) % TAR_BLOCK_SIZE;
        if (pad)
        {
            std::vector<char> zeros(pad, 0);
            gzwrite(out, zeros.data(), static_cast<unsigned int>(pad));
        }
    };

    auto writeTarEntry = [&](const std::string& archivePath,
                             const uint8_t* fileData, uint64_t fileSize,
                             uint32_t perms, uint64_t mtime)
    {
        std::string name = archivePath;
        std::string prefix;
        size_t slash = name.rfind('/');
        if (slash != std::string::npos && slash <= TAR_PREFIX_SIZE)
        {
            prefix = name.substr(0, slash);
            name = name.substr(slash + 1);
        }
        if (name.size() >= TAR_NAME_SIZE)
            name.resize(TAR_NAME_SIZE - 1);
        if (prefix.size() >= TAR_PREFIX_SIZE)
            prefix.resize(TAR_PREFIX_SIZE - 1);

        TarHeader hdr = {};
        std::memcpy(hdr.name, name.c_str(), name.size());
        std::memcpy(hdr.prefix, prefix.c_str(), prefix.size());
        FormatOctal(hdr.mode, 8, perms ? perms : 0644);
        FormatOctal(hdr.size, 12, fileSize);
        FormatOctal(hdr.mtime, 12, mtime);
        hdr.typeflag = '0';
        std::memcpy(hdr.magic, "ustar", 5);
        std::memcpy(hdr.version, "00", 2);
        FormatOctal(hdr.chksum, 8, 0);
        FormatOctal(hdr.chksum, 8, ComputeChecksum(&hdr));

        writeBlock(&hdr, sizeof(hdr));
        if (fileSize > 0)
            writeBlock(fileData, static_cast<size_t>(fileSize));
    };

    int fileIdx = 0;
    uint64_t bytesDone = 0;

    // Write preserved existing entries
    for (const auto& me : merged)
    {
        if (m_saveCancelled)
        {
            gzclose(out);
            LOG_DBG("TarGzEngine: save cancelled");
            return false;
        }

        if (m_saveProgressCb)
        {
            SaveProgressInfo info;
            info.currentFile = fileIdx;
            info.totalFiles = totalTarFiles;
            info.bytesProcessed = bytesDone;
            info.totalBytes = totalTarBytes;
            info.fileName = me.archivePath;
            m_saveProgressCb(info);
        }

        writeTarEntry(me.archivePath, me.data.data(), me.data.size(),
                      me.permissions, me.mtime);
        bytesDone += me.data.size();
        fileIdx++;
    }

    // Write newly added entries from queue
    for (const auto& qe : m_entryQueue)
    {
        if (m_saveCancelled)
        {
            gzclose(out);
            LOG_DBG("TarGzEngine: save cancelled");
            return false;
        }

        if (m_saveProgressCb)
        {
            SaveProgressInfo info;
            info.currentFile = fileIdx;
            info.totalFiles = totalTarFiles;
            info.bytesProcessed = bytesDone;
            info.totalBytes = totalTarBytes;
            info.fileName = qe.archivePath;
            m_saveProgressCb(info);
        }

        std::ifstream in(qe.srcPath, std::ios::binary | std::ios::ate);
        if (!in)
        {
            LOG_WARN("TarGzEngine: cannot read %s", qe.srcPath.c_str());
            continue;
        }

        uint64_t fileSize = static_cast<uint64_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
        in.read(reinterpret_cast<char*>(fileData.data()), fileData.size());

        uint32_t perms = 0644;
        {
            std::error_code ec;
            auto srcPerms = fs::status(qe.srcPath, ec).permissions();
            if (!ec && srcPerms != fs::perms::unknown)
                perms = static_cast<uint32_t>(srcPerms) & 0777;
        }

        auto now = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now()));

        writeTarEntry(qe.archivePath, fileData.data(), fileSize, perms, now);
        bytesDone += fileSize;
        fileIdx++;
    }

    // End-of-archive: two zero blocks
    std::vector<char> endBlock(TAR_BLOCK_SIZE * 2, 0);
    gzwrite(out, endBlock.data(), static_cast<unsigned int>(endBlock.size()));

    gzclose(out);

    // Re-open to refresh entry list
    Open(m_path);

    LOG_DBG("TarGzEngine: saved successfully (%zu entries)", m_entries.size());
    return true;
}

// ── Testing ────────────────────────────────────────────────────────────
bool TarGzEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    gzFile f = gzopen(m_path.c_str(), "rb");
    if (!f)
    {
        return false;
    }

    int total = (int)m_entries.size();
    int current = 0;

    TarHeader hdr;
    while (true)
    {
        if (cancelFlag && cancelFlag())
        {
            gzclose(f);
            return false;
        }
        if (progressCallback) progressCallback(current, total);

        int bytesRead = gzread(f, &hdr, TAR_BLOCK_SIZE);
        if (bytesRead < static_cast<int>(TAR_BLOCK_SIZE))
        {
            break;
        }

        if (IsEndOfArchive(&hdr))
        {
            break;
        }

        if (!ValidateChecksum(&hdr))
        {
            gzclose(f);
            return false;
        }

        uint64_t fileSize = ParseOctal(hdr.size, 12);
        uint64_t paddedSize = ((fileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;

        std::vector<char> skipBuf(static_cast<size_t>(
            std::min(paddedSize, static_cast<uint64_t>(65536))));
        while (paddedSize > 0)
        {
            size_t toRead = static_cast<size_t>(
                std::min(paddedSize, static_cast<uint64_t>(skipBuf.size())));
            int r = gzread(f, skipBuf.data(),
                           static_cast<unsigned int>(toRead));
            if (r <= 0) break;
            paddedSize -= static_cast<uint64_t>(r);
        }
        current++;
    }

    gzclose(f);
    return true;
}
