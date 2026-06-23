#include "TarGzEngine.h"

#include "Logging.h"

#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <fstream>
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

// ── Lifecycle ──────────────────────────────────────────────────────────
TarGzEngine::~TarGzEngine()
{
    TarGzEngine::Close();
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

    LOG_DBG("TarGzEngine: reading entries from %s", m_path.c_str());

    // Read all entries
    TarHeader hdr;
    while (true)
    {
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
            break;
        }

        uint64_t fileSize = ParseOctal(hdr.size, 12);
        std::string entryName = TarName(&hdr);

        ArchiveEntry entry;
        entry.name = entryName;
        entry.path = entryName;
        entry.size = fileSize;
        entry.packedSize = fileSize;
        entry.isDirectory = (hdr.typeflag == '5');
        entry.permissions = ParseOctal(hdr.mode, 8) & 0xFFF;

        struct tm timeinfo = {};
        uint64_t mtime = ParseOctal(hdr.mtime, 12);
        entry.modified = std::chrono::system_clock::from_time_t(
            static_cast<time_t>(mtime));

        m_entries.push_back(std::move(entry));

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
}

// ── Reading ────────────────────────────────────────────────────────────
std::vector<ArchiveEntry> TarGzEngine::ListContents()
{
    return m_entries;
}

bool TarGzEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& entry : m_entries)
    {
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
    {
        return {};
    }

    gzclose(m_gzFile);
    m_gzFile = gzopen(m_path.c_str(), "rb");
    if (!m_gzFile)
    {
        return {};
    }

    TarHeader hdr;
    while (true)
    {
        int bytesRead = gzread(m_gzFile, &hdr, TAR_BLOCK_SIZE);
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
            break;
        }

        uint64_t fileSize = ParseOctal(hdr.size, 12);
        std::string currentName = TarName(&hdr);

        uint64_t paddedSize = ((fileSize + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;

        if (currentName == entryName)
        {
            std::vector<uint8_t> result(static_cast<size_t>(fileSize));
            if (fileSize > 0)
            {
                int r = gzread(m_gzFile, result.data(),
                               static_cast<unsigned int>(fileSize));
                if (r <= 0)
                {
                    return {};
                }
            }
            return result;
        }

        std::vector<char> skipBuf(static_cast<size_t>(
            std::min(paddedSize, static_cast<uint64_t>(65536))));
        while (paddedSize > 0)
        {
            size_t toRead = static_cast<size_t>(
                std::min(paddedSize, static_cast<uint64_t>(skipBuf.size())));
            int r = gzread(m_gzFile, skipBuf.data(),
                           static_cast<unsigned int>(toRead));
            if (r <= 0) break;
            paddedSize -= static_cast<uint64_t>(r);
        }
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
    m_modified = true;
    return true;
}

bool TarGzEngine::Save()
{
    if (!m_modified && m_entryQueue.empty())
    {
        return true;
    }

    LOG_DBG("TarGzEngine: saving %s (%zu queued files)", m_path.c_str(), m_entryQueue.size());

    gzFile out = gzopen(m_path.c_str(), "wb");
    if (!out)
    {
        LOG_ERR("TarGzEngine: cannot write %s", m_path.c_str());
        return false;
    }

    auto writeBlock = [&](const void* data, size_t size)
    {
        gzwrite(out, data, static_cast<unsigned int>(size));
        // Pad to block boundary
        size_t pad = (TAR_BLOCK_SIZE - size % TAR_BLOCK_SIZE) % TAR_BLOCK_SIZE;
        if (pad)
        {
            std::vector<char> zeros(pad, 0);
            gzwrite(out, zeros.data(), static_cast<unsigned int>(pad));
        }
    };

    for (const auto& qe : m_entryQueue)
    {
        std::string name = qe.archivePath;
        std::string prefix;
        size_t slash = name.rfind('/');
        if (slash != std::string::npos && slash <= TAR_PREFIX_SIZE)
        {
            prefix = name.substr(0, slash);
            name = name.substr(slash + 1);
        }
        if (name.size() >= TAR_NAME_SIZE)
        {
            LOG_WARN("TarGzEngine: filename too long, truncated: %s", name.c_str());
            name.resize(TAR_NAME_SIZE - 1);
        }
        if (prefix.size() >= TAR_PREFIX_SIZE)
        {
            LOG_WARN("TarGzEngine: path too long, truncated: %s", prefix.c_str());
            prefix.resize(TAR_PREFIX_SIZE - 1);
        }

        std::ifstream in(qe.srcPath, std::ios::binary | std::ios::ate);
        if (!in)
        {
            LOG_WARN("TarGzEngine: cannot read %s", qe.srcPath.c_str());
            continue;
        }

        uint64_t fileSize = static_cast<uint64_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        auto fileData = std::vector<uint8_t>(static_cast<size_t>(fileSize));
        in.read(reinterpret_cast<char*>(fileData.data()), fileData.size());

        TarHeader hdr = {};
        std::memcpy(hdr.name, name.c_str(), name.size());
        std::memcpy(hdr.prefix, prefix.c_str(), prefix.size());
        FormatOctal(hdr.mode, 8, 0644);
        FormatOctal(hdr.size, 12, fileSize);

        auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        FormatOctal(hdr.mtime, 12, static_cast<uint64_t>(now));

        hdr.typeflag = '0';
        std::memcpy(hdr.magic, "ustar", 5);
        std::memcpy(hdr.version, "00", 2);

        FormatOctal(hdr.chksum, 8, 0);
        FormatOctal(hdr.chksum, 8, ComputeChecksum(&hdr));

        writeBlock(&hdr, sizeof(hdr));
        writeBlock(fileData.data(), fileData.size());
    }

    // End-of-archive: two zero blocks
    std::vector<char> endBlock(TAR_BLOCK_SIZE * 2, 0);
    gzwrite(out, endBlock.data(), static_cast<unsigned int>(endBlock.size()));

    gzclose(out);
    LOG_DBG("TarGzEngine: saved successfully");
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
