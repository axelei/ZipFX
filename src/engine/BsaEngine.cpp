#include "BsaEngine.h"
#include "Logging.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint64_t readU64(const uint8_t* p)
{
    return uint64_t(readU32(p)) | (uint64_t(readU32(p + 4)) << 32);
}

bool BsaEngine::Open(std::string_view path)
{
    Close();
    m_path = std::string(path);

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    uint8_t hdr[36];
    if (!f.read(reinterpret_cast<char*>(hdr), 36)) return false;
    if (std::memcmp(hdr, "BSA\0", 4) != 0) return false;

    uint32_t version     = readU32(hdr + 4);
    uint32_t archiveFlags = readU32(hdr + 12);
    uint32_t folderCount  = readU32(hdr + 16);
    uint32_t fileCount    = readU32(hdr + 20);
    uint32_t totalFNameLen = readU32(hdr + 28);

    // 103 = Morrowind (different layout), 104 = Oblivion/FO3/FONV/Skyrim LE, 105 = Skyrim SE
    if (version != 104 && version != 105)
    {
        LOG_WARN("BSA: unsupported version %u in %s", version, m_path.c_str());
        return false;
    }

    bool defaultCompressed = (archiveFlags & 0x0004) != 0;
    bool embedFileNames    = (archiveFlags & 0x0100) != 0;
    bool isV105            = (version == 105);

    // Folder records: 16 bytes (v104) or 24 bytes (v105)
    uint32_t folderRecSize = isV105 ? 24 : 16;

    struct FolderRec { uint32_t fileCount; };
    std::vector<FolderRec> folderRecs(folderCount);

    for (uint32_t i = 0; i < folderCount; ++i)
    {
        uint8_t buf[24] = {};
        if (!f.read(reinterpret_cast<char*>(buf), folderRecSize)) return false;
        folderRecs[i].fileCount = readU32(buf + 8);
    }

    // File record blocks follow folder records. Each block:
    //   uint8_t  bzLen         (folder name length including \0)
    //   char[bzLen] folderName (null-terminated)
    //   N * 16 bytes  file records
    struct RawFileRec { uint32_t rawSize; uint32_t dataOffset; };
    std::vector<std::string> folderNames(folderCount);
    std::vector<std::vector<RawFileRec>> fileSets(folderCount);

    for (uint32_t i = 0; i < folderCount; ++i)
    {
        uint8_t bzLen = 0;
        if (!f.read(reinterpret_cast<char*>(&bzLen), 1)) return false;
        std::string fname(bzLen, '\0');
        if (bzLen > 0 && !f.read(fname.data(), bzLen)) return false;
        while (!fname.empty() && fname.back() == '\0') fname.pop_back();
        std::replace(fname.begin(), fname.end(), '\\', '/');
        folderNames[i] = std::move(fname);

        uint32_t fc = folderRecs[i].fileCount;
        fileSets[i].resize(fc);
        for (uint32_t j = 0; j < fc; ++j)
        {
            uint8_t buf[16];
            if (!f.read(reinterpret_cast<char*>(buf), 16)) return false;
            // hash (8 bytes) is ignored; size at [8], offset at [12]
            fileSets[i][j].rawSize    = readU32(buf + 8);
            fileSets[i][j].dataOffset = readU32(buf + 12);
        }
    }

    // File names block
    std::vector<std::string> fileNames;
    if (totalFNameLen > 0)
    {
        std::string block(totalFNameLen, '\0');
        if (!f.read(block.data(), totalFNameLen)) return false;
        size_t pos = 0;
        while (pos < totalFNameLen)
        {
            size_t end = block.find('\0', pos);
            if (end == std::string::npos) { fileNames.push_back(block.substr(pos)); break; }
            if (end > pos) fileNames.push_back(block.substr(pos, end - pos));
            else           fileNames.emplace_back();
            pos = end + 1;
        }
    }

    // Build FileRecord entries, seeking to compute dataStart
    m_fileRecords.reserve(fileCount);
    m_entries.reserve(fileCount);
    size_t globalIdx = 0;

    for (uint32_t i = 0; i < folderCount; ++i)
    {
        for (uint32_t j = 0; j < folderRecs[i].fileCount; ++j, ++globalIdx)
        {
            const auto& raw = fileSets[i][j];
            bool flipComp   = (raw.rawSize & 0x40000000) != 0;
            uint32_t sz     = raw.rawSize & 0x3FFFFFFF;
            bool compressed = defaultCompressed ^ flipComp;

            std::string fileName = (globalIdx < fileNames.size())
                ? fileNames[globalIdx]
                : ("file_" + std::to_string(globalIdx));

            std::string fullPath = folderNames[i].empty()
                ? fileName
                : (folderNames[i] + "/" + fileName);

            // Seek to data start to compute actual payload position
            uint32_t dataStart = raw.dataOffset;
            uint32_t dataSize  = sz;
            uint32_t origSize  = 0;

            f.seekg(dataStart);
            if (f)
            {
                if (embedFileNames)
                {
                    uint8_t nameLen = 0;
                    f.read(reinterpret_cast<char*>(&nameLen), 1);
                    f.seekg(nameLen, std::ios::cur);
                    dataStart = static_cast<uint32_t>(f.tellg());
                    dataSize  = (sz > 1u + nameLen) ? (sz - 1u - nameLen) : 0u;
                }
                if (compressed && dataSize >= 4)
                {
                    uint8_t buf4[4];
                    f.read(reinterpret_cast<char*>(buf4), 4);
                    origSize   = readU32(buf4);
                    dataStart  = static_cast<uint32_t>(f.tellg());
                    dataSize  -= 4;
                }
            }

            FileRecord rec;
            rec.fullPath   = fullPath;
            rec.dataStart  = dataStart;
            rec.dataSize   = dataSize;
            rec.origSize   = origSize;
            rec.compressed = compressed;
            m_fileRecords.push_back(rec);

            ArchiveEntry ae;
            ae.name              = fullPath;
            ae.path              = fullPath;
            ae.size              = compressed ? origSize : dataSize;
            ae.packedSize        = dataSize;
            ae.isDirectory       = false;
            ae.compressionMethod = compressed ? "zlib" : "";
            m_entries.push_back(std::move(ae));
        }
    }

    m_formatName = isV105 ? "BSA (SE)" : "BSA";
    m_isOpen     = true;
    LOG_DBG("BSA: opened %s (%zu entries, v%u)", m_path.c_str(), m_entries.size(), version);
    return true;
}

void BsaEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_fileRecords.clear();
    m_path.clear();
}

const std::vector<ArchiveEntry>& BsaEngine::ListContents()
{
    return m_entries;
}

std::vector<uint8_t> BsaEngine::doRead(const FileRecord& rec, size_t maxBytes)
{
    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};

    f.seekg(rec.dataStart);
    if (!f) return {};

    if (rec.compressed)
    {
        std::vector<uint8_t> comp(rec.dataSize);
        if (!f.read(reinterpret_cast<char*>(comp.data()), rec.dataSize)) return {};

        if (rec.origSize == 0) return {};

        std::vector<uint8_t> out(rec.origSize);
        uLongf destLen = rec.origSize;
        if (uncompress(out.data(), &destLen, comp.data(), rec.dataSize) != Z_OK)
        {
            LOG_WARN("BSA: zlib decompress failed for entry, size %u->%u", rec.dataSize, rec.origSize);
            return {};
        }
        out.resize(destLen);
        if (maxBytes < out.size()) out.resize(maxBytes);
        return out;
    }
    else
    {
        size_t readSize = std::min((size_t)rec.dataSize, maxBytes);
        std::vector<uint8_t> out(readSize);
        f.read(reinterpret_cast<char*>(out.data()), readSize);
        out.resize(static_cast<size_t>(f.gcount()));
        return out;
    }
}

std::vector<uint8_t> BsaEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    m_extractCancelled = false;

    for (const auto& rec : m_fileRecords)
    {
        if (m_extractCancelled) return {};
        if (rec.fullPath == entryName)
            return doRead(rec, SIZE_MAX);
    }
    LOG_WARN("BSA: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

std::vector<uint8_t> BsaEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};

    for (const auto& rec : m_fileRecords)
    {
        if (rec.fullPath == entryName)
            return doRead(rec, maxBytes);
    }
    return {};
}

bool BsaEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    for (const auto& rec : m_fileRecords)
    {
        if (m_extractCancelled) return false;
        if (rec.fullPath != entryName) continue;

        auto data = doRead(rec, SIZE_MAX);
        if (data.empty() && rec.dataSize > 0) return false;

        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        return out.good();
    }
    return false;
}

bool BsaEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    m_extractCancelled = false;

    fs::path base(destPath);
    for (const auto& rec : m_fileRecords)
    {
        if (m_extractCancelled) return false;

        auto data = doRead(rec, SIZE_MAX);

        fs::path dest = base / rec.fullPath;
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out)
        {
            LOG_ERR("BSA: cannot create %s", dest.string().c_str());
            return false;
        }
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!out.good()) return false;
    }
    return true;
}

bool BsaEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;
    int total = static_cast<int>(m_fileRecords.size());
    int current = 0;
    for (const auto& rec : m_fileRecords)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(current, total);
        auto data = doRead(rec, SIZE_MAX);
        if (data.empty() && rec.dataSize > 0) return false;
        ++current;
    }
    return true;
}
