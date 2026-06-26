#include "ChdEngine.h"
#include "Logging.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static uint32_t sectorSizeForType(const char* type)
{
    if (std::strcmp(type, "MODE1")     == 0) return 2048;
    if (std::strcmp(type, "MODE1_RAW") == 0) return 2352;
    if (std::strcmp(type, "MODE2")     == 0) return 2336;
    if (std::strcmp(type, "MODE2_FORM1") == 0) return 2048;
    if (std::strcmp(type, "MODE2_FORM2") == 0) return 2328;
    if (std::strcmp(type, "MODE2_FORM_MIX") == 0) return 2336;
    if (std::strcmp(type, "MODE2_RAW") == 0) return 2352;
    if (std::strcmp(type, "AUDIO")     == 0) return 2352;
    return 2048;
}

ChdEngine::~ChdEngine()
{
    ChdEngine::Close();
}

bool ChdEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    chd_error err = chd_open(m_path.c_str(), CHD_OPEN_READ, nullptr, &m_chd);
    if (err != CHDERR_NONE)
    {
        LOG_ERR("CHD: failed to open %s: %s", m_path.c_str(), chd_error_string(err));
        m_chd = nullptr;
        return false;
    }

    const chd_header* hdr = chd_get_header(m_chd);
    m_hunkBytes = hdr->hunkbytes;
    m_logicalBytes = hdr->logicalbytes;

    parseMetadata();

    m_entries.clear();
    for (const auto& t : m_tracks)
    {
        ArchiveEntry ae;
        ae.name = t.name;
        ae.path = t.name;
        ae.size = t.size;
        ae.packedSize = 0;
        ae.isDirectory = false;
        ae.compressionMethod = t.type;
        m_entries.push_back(std::move(ae));
    }

    // Build a descriptive format label from the disc type
    if (!m_tracks.empty())
    {
        const auto& type = m_tracks[0].type;
        if (type == "DVD")
            m_formatLabel = "CHD (DVD)";
        else if (type == "HDD")
            m_formatLabel = "CHD (HDD)";
        else if (type == "RAW")
            m_formatLabel = "CHD (Raw)";
        else
        {
            bool hasAudio = false, hasData = false;
            for (const auto& t : m_tracks)
            {
                if (t.type == "AUDIO") hasAudio = true;
                else hasData = true;
            }
            if (m_tracks.size() == 1 && hasData)
                m_formatLabel = "CHD (ISO)";
            else if (hasData && hasAudio)
                m_formatLabel = "CHD (Mixed CD)";
            else if (hasAudio)
                m_formatLabel = "CHD (Audio CD)";
            else
                m_formatLabel = "CHD (CD)";
        }
    }

    LOG_DBG("CHD: opened %s (%zu entries, %s)", m_path.c_str(), m_entries.size(), m_formatLabel.c_str());
    return true;
}

void ChdEngine::parseMetadata()
{
    m_tracks.clear();

    // Try CD-ROM track metadata (CHTR / CHT2)
    char metaBuf[256];
    uint32_t metaLen = 0;
    uint32_t metaTag = 0;
    uint64_t byteOffset = 0;

    for (uint32_t idx = 0; ; ++idx)
    {
        // Try CDROM_TRACK_METADATA2 first, fall back to CDROM_TRACK_METADATA
        chd_error err = chd_get_metadata(m_chd, 0x43485432 /*CHT2*/, idx,
                                          metaBuf, sizeof(metaBuf) - 1,
                                          &metaLen, &metaTag, nullptr);
        if (err != CHDERR_NONE)
            err = chd_get_metadata(m_chd, 0x43485452 /*CHTR*/, idx,
                                   metaBuf, sizeof(metaBuf) - 1,
                                   &metaLen, &metaTag, nullptr);
        if (err != CHDERR_NONE)
            break;

        metaBuf[metaLen] = '\0';

        int trackNum = 0;
        char type[32] = {};
        char subtype[32] = {};
        int frames = 0;
        std::sscanf(metaBuf, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d",
                    &trackNum, type, subtype, &frames);

        if (frames <= 0) continue;

        uint32_t secSize = sectorSizeForType(type);

        TrackInfo ti;
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.bin", trackNum);
        ti.name = nameBuf;
        ti.type = type;
        ti.frames = static_cast<uint32_t>(frames);
        ti.sectorSize = secSize;
        ti.offset = byteOffset;
        ti.size = static_cast<uint64_t>(frames) * secSize;

        byteOffset += static_cast<uint64_t>(frames) * secSize;
        m_tracks.push_back(std::move(ti));
    }

    if (!m_tracks.empty())
        return;

    // Try hard disk metadata
    chd_error err = chd_get_metadata(m_chd, 0x47444444 /*GDDD*/, 0,
                                      metaBuf, sizeof(metaBuf) - 1,
                                      &metaLen, &metaTag, nullptr);
    if (err == CHDERR_NONE)
    {
        metaBuf[metaLen] = '\0';
        TrackInfo ti;
        ti.name = fs::path(m_path).stem().string() + ".img";
        ti.type = "HDD";
        ti.sectorSize = 0;
        ti.offset = 0;
        ti.size = m_logicalBytes;
        m_tracks.push_back(std::move(ti));
        return;
    }

    // Try DVD metadata
    err = chd_get_metadata(m_chd, 0x44564420 /*DVD */, 0,
                            metaBuf, sizeof(metaBuf) - 1,
                            &metaLen, &metaTag, nullptr);
    if (err == CHDERR_NONE)
    {
        TrackInfo ti;
        ti.name = fs::path(m_path).stem().string() + ".iso";
        ti.type = "DVD";
        ti.sectorSize = 0;
        ti.offset = 0;
        ti.size = m_logicalBytes;
        m_tracks.push_back(std::move(ti));
        return;
    }

    // Fallback: present entire image as single raw file
    TrackInfo ti;
    ti.name = fs::path(m_path).stem().string() + ".img";
    ti.type = "RAW";
    ti.sectorSize = 0;
    ti.offset = 0;
    ti.size = m_logicalBytes;
    m_tracks.push_back(std::move(ti));
}

std::vector<uint8_t> ChdEngine::readRange(uint64_t offset, uint64_t length)
{
    if (!m_chd || m_hunkBytes == 0 || length == 0)
        return {};

    m_extractCancelled = false;
    std::vector<uint8_t> result(length);
    std::vector<uint8_t> hunkBuf(m_hunkBytes);

    uint64_t bytesRead = 0;
    while (bytesRead < length)
    {
        if (m_extractCancelled) { LOG_DBG("CHD: extract cancelled"); return {}; }
        uint64_t absPos = offset + bytesRead;
        uint32_t hunkNum = static_cast<uint32_t>(absPos / m_hunkBytes);
        uint32_t hunkOff = static_cast<uint32_t>(absPos % m_hunkBytes);

        chd_error err = chd_read(m_chd, hunkNum, hunkBuf.data());
        if (err != CHDERR_NONE)
        {
            LOG_WARN("CHD: error reading hunk %u: %s", hunkNum, chd_error_string(err));
            result.resize(bytesRead);
            return result;
        }

        uint64_t toCopy = std::min<uint64_t>(m_hunkBytes - hunkOff, length - bytesRead);
        std::memcpy(result.data() + bytesRead, hunkBuf.data() + hunkOff, toCopy);
        bytesRead += toCopy;
    }

    return result;
}

void ChdEngine::Close()
{
    if (m_chd)
    {
        chd_close(m_chd);
        m_chd = nullptr;
    }
    m_tracks.clear();
    m_entries.clear();
}

const std::vector<ArchiveEntry>& ChdEngine::ListContents()
{
    return m_entries;
}

std::vector<uint8_t> ChdEngine::ReadFile(std::string_view entryName)
{
    for (const auto& t : m_tracks)
    {
        if (t.name == entryName)
            return readRange(t.offset, t.size);
    }
    LOG_WARN("CHD: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

bool ChdEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_chd || m_hunkBytes == 0) return false;
    m_extractCancelled = false;

    const TrackInfo* track = nullptr;
    for (const auto& t : m_tracks)
        if (t.name == entryName) { track = &t; break; }
    if (!track) { LOG_WARN("CHD: entry '%.*s' not found", (int)entryName.size(), entryName.data()); return false; }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) { LOG_ERR("CHD: cannot create %s", dest.string().c_str()); return false; }

    std::vector<uint8_t> hunkBuf(m_hunkBytes);
    uint64_t remaining = track->size;
    uint64_t pos = track->offset;

    while (remaining > 0)
    {
        if (m_extractCancelled) return false;

        uint32_t hunkNum = static_cast<uint32_t>(pos / m_hunkBytes);
        uint32_t hunkOff = static_cast<uint32_t>(pos % m_hunkBytes);

        chd_error err = chd_read(m_chd, hunkNum, hunkBuf.data());
        if (err != CHDERR_NONE) { LOG_WARN("CHD: error reading hunk %u", hunkNum); return false; }

        uint64_t toCopy = std::min<uint64_t>(m_hunkBytes - hunkOff, remaining);
        out.write(reinterpret_cast<const char*>(hunkBuf.data() + hunkOff), toCopy);
        pos += toCopy;
        remaining -= toCopy;
    }
    return out.good();
}

bool ChdEngine::ExtractAll(std::string_view destPath)
{
    m_extractCancelled = false;

    for (const auto& t : m_tracks)
    {
        if (m_extractCancelled) { LOG_DBG("CHD: extract cancelled"); return false; }

        fs::path outPath = fs::path(destPath) / t.name;
        fs::create_directories(outPath.parent_path());

        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            LOG_ERR("CHD: cannot create %s", outPath.string().c_str());
            return false;
        }

        std::vector<uint8_t> hunkBuf(m_hunkBytes);
        uint64_t remaining = t.size;
        uint64_t pos = t.offset;

        while (remaining > 0)
        {
            if (m_extractCancelled) { LOG_DBG("CHD: extract cancelled"); return false; }

            uint32_t hunkNum = static_cast<uint32_t>(pos / m_hunkBytes);
            uint32_t hunkOff = static_cast<uint32_t>(pos % m_hunkBytes);

            chd_error err = chd_read(m_chd, hunkNum, hunkBuf.data());
            if (err != CHDERR_NONE)
            {
                LOG_WARN("CHD: error reading hunk %u", hunkNum);
                return false;
            }

            uint64_t toCopy = std::min<uint64_t>(m_hunkBytes - hunkOff, remaining);
            out.write(reinterpret_cast<const char*>(hunkBuf.data() + hunkOff), toCopy);
            pos += toCopy;
            remaining -= toCopy;
        }
    }
    return true;
}

bool ChdEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_chd) return false;

    uint32_t totalHunks = static_cast<uint32_t>(m_logicalBytes / m_hunkBytes);
    std::vector<uint8_t> hunkBuf(m_hunkBytes);

    for (uint32_t i = 0; i < totalHunks; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(static_cast<int>(i), static_cast<int>(totalHunks));

        chd_error err = chd_read(m_chd, i, hunkBuf.data());
        if (err != CHDERR_NONE)
            return false;
    }
    return true;
}
