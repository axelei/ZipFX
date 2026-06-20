#include "TarGzEngine.h"

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

static bool ValidateChecksum(const TarHeader* hdr)
{
    // The checksum field is treated as spaces when computing
    uint64_t stored = ParseOctal(hdr->chksum, 8);

    uint64_t computed = 0;
    const char* p = reinterpret_cast<const char*>(hdr);
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i)
    {
        if (i >= 148 && i < 156)
        {
            computed += ' '; // checksum field treated as spaces
        }
        else
        {
            computed += static_cast<unsigned char>(p[i]);
        }
    }

    return stored == computed;
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
    Close();
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

        // Skip non-regular files (directories, symlinks, etc.)
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

        struct tm timeinfo = {};
        timeinfo.tm_sec  = 0;
        // Parse mtime from octal
        uint64_t mtime = ParseOctal(hdr.mtime, 12);
        entry.modified = std::chrono::system_clock::from_time_t(
            static_cast<time_t>(mtime));

        m_entries.push_back(std::move(entry));

        // Skip file data (padded to block boundary)
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

bool TarGzEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen)
    {
        return false;
    }

    auto data = ReadFile(entryName);
    if (data.empty())
    {
        return false;
    }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out)
    {
        return false;
    }

    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
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

    // Re-open and scan
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

        // Skip data
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
    return true;
}

bool TarGzEngine::RemoveEntry(std::string_view entryName)
{
    m_modified = true;
    return true;
}

bool TarGzEngine::Save()
{
    // Not yet implemented — writing tar.gz requires rebuilding
    // the entire archive from the cached entries.
    // For now, return false to indicate it's not functional yet.
    (void)0;
    return false;
}

// ── Testing ────────────────────────────────────────────────────────────
bool TarGzEngine::TestIntegrity()
{
    // Re-open and verify checksums
    gzFile f = gzopen(m_path.c_str(), "rb");
    if (!f)
    {
        return false;
    }

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
    }

    gzclose(f);
    return true;
}
