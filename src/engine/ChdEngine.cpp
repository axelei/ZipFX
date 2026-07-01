#include "ChdEngine.h"
#include "Logging.h"
#include "WavHeader.h"

#include <libchdr/chd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// CD-ROM tracks in a CHD are always physically stored as fixed-size frames:
// a full reconstructed 2352-byte raw sector followed by 96 bytes of subcode,
// regardless of what the declared TYPE says the "cooked" output should look
// like. See libchdr/cdrom.h: CD_MAX_SECTOR_DATA(2352) + CD_MAX_SUBCODE_DATA(96).
static constexpr uint32_t kCdSectorBytes = 2352;
static constexpr uint32_t kCdSubcodeBytes = 96;
static constexpr uint32_t kCdFrameBytes = kCdSectorBytes + kCdSubcodeBytes;

// Returns the offset within the 2352-byte raw sector where this track's
// "cooked" user data begins, and how many bytes of it there are.
static void cdLayoutForType(const std::string& type, uint32_t& headerOff, uint32_t& userSize)
{
    if (type == "MODE1")               { headerOff = 16; userSize = 2048; }
    else if (type == "MODE1_RAW")      { headerOff = 0;  userSize = 2352; }
    else if (type == "MODE2")          { headerOff = 16; userSize = 2336; }
    else if (type == "MODE2_FORM1")    { headerOff = 24; userSize = 2048; }
    else if (type == "MODE2_FORM2")    { headerOff = 24; userSize = 2328; }
    else if (type == "MODE2_FORM_MIX") { headerOff = 16; userSize = 2336; }
    else if (type == "MODE2_RAW")      { headerOff = 0;  userSize = 2352; }
    else if (type == "AUDIO")          { headerOff = 0;  userSize = 2352; }
    else                                { headerOff = 0;  userSize = 2048; }
}

// CD-DA audio tracks are raw 16-bit/44100Hz/stereo PCM with no container —
// wrap them in a standard WAV header so media players and file managers can
// recognize and play them directly.
static WavHeader buildCdAudioWavHeader(uint32_t pcmBytes)
{
    WavHeader h;
    h.numChannels   = 2;
    h.bitsPerSample = 16;
    h.blockAlign    = h.numChannels * (h.bitsPerSample / 8);
    h.byteRate      = h.sampleRate * h.blockAlign;
    h.dataSize      = pcmBytes;
    h.fileSize      = static_cast<uint32_t>(sizeof(WavHeader) - 8 + pcmBytes);
    return h;
}

static uint64_t entrySizeForTrack(uint64_t size, const std::string& type)
{
    return type == "AUDIO" ? size + sizeof(WavHeader) : size;
}

// Red Book CD-DA samples are stored big-endian on disc (and in CHD CD-ROM
// tracks), while WAV/PCM expects little-endian — swap each 16-bit sample.
static void swapAudioSamples(std::vector<uint8_t>& pcm)
{
    for (size_t i = 0; i + 1 < pcm.size(); i += 2)
        std::swap(pcm[i], pcm[i + 1]);
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

    m_hasFilesystem = tryMountFilesystem();
    if (!m_hasFilesystem)
    {
        // Fallback: no recognizable ISO 9660 filesystem (unusual/unsupported
        // disc filesystem, non-CD blob, etc.) — expose the raw tracks plus a
        // synthesized .cue sheet so the data can still be used elsewhere.
        m_entries.clear();
        for (const auto& t : m_tracks)
        {
            ArchiveEntry ae;
            ae.name = t.name;
            ae.path = t.name;
            ae.size = entrySizeForTrack(t.size, t.type);
            ae.packedSize = 0;
            ae.isDirectory = false;
            ae.compressionMethod = t.type;
            m_entries.push_back(std::move(ae));
        }

        buildCueSheet();
        if (!m_cueSheetName.empty())
        {
            ArchiveEntry ae;
            ae.name = m_cueSheetName;
            ae.path = m_cueSheetName;
            ae.size = m_cueSheetText.size();
            ae.packedSize = ae.size;
            ae.isDirectory = false;
            ae.compressionMethod = "CUE";
            m_entries.push_back(std::move(ae));
        }
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
    uint64_t frameCursor = 0;

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

        uint32_t headerOff = 0, userSize = 0;
        cdLayoutForType(type, headerOff, userSize);

        TrackInfo ti;
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.%s", trackNum,
                      std::strcmp(type, "AUDIO") == 0 ? "wav" : "bin");
        ti.name = nameBuf;
        ti.type = type;
        ti.number = trackNum;
        ti.frames = static_cast<uint32_t>(frames);
        ti.isCdTrack = true;
        ti.startFrame = frameCursor;
        ti.headerOff = headerOff;
        ti.userSize = userSize;
        ti.size = static_cast<uint64_t>(frames) * userSize;

        frameCursor += static_cast<uint64_t>(frames);
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
        ti.offset = 0;
        ti.size = m_logicalBytes;
        m_tracks.push_back(std::move(ti));
        return;
    }

    // Fallback: present entire image as single raw file
    TrackInfo ti;
    ti.name = fs::path(m_path).stem().string() + ".img";
    ti.type = "RAW";
    ti.offset = 0;
    ti.size = m_logicalBytes;
    m_tracks.push_back(std::move(ti));
}

bool ChdEngine::tryMountFilesystem()
{
    // Find the first non-audio CD track — the data area that should hold the
    // ISO 9660 filesystem. PS1/PS2/Saturn/etc discs put this at track 1.
    const TrackInfo* dataTrack = nullptr;
    for (const auto& t : m_tracks)
    {
        if (t.isCdTrack && t.type != "AUDIO") { dataTrack = &t; break; }
    }
    if (!dataTrack) return false;

    // ISO 9660 logical sectors are always 2048 bytes, one per CD frame.
    // Mode 1 sectors have no subheader (user data at raw-sector offset 16);
    // Mode 2 (XA) sectors have an 8-byte subheader (user data at offset 24).
    bool isMode2 = dataTrack->type.rfind("MODE2", 0) == 0;
    uint32_t isoHeaderOff = isMode2 ? 24 : 16;
    uint64_t startFrame = dataTrack->startFrame;

    auto sectorFn = [this, startFrame, isoHeaderOff](uint32_t lba, uint8_t* out) -> bool
    {
        uint64_t off = (startFrame + lba) * kCdFrameBytes + isoHeaderOff;
        auto data = readRange(off, 2048);
        if (data.size() != 2048) return false;
        std::memcpy(out, data.data(), 2048);
        return true;
    };

    if (!m_iso.open(sectorFn))
        return false;

    m_entries.clear();
    for (const auto& e : m_iso.entries())
    {
        ArchiveEntry ae;
        ae.name        = e.path;
        ae.path        = e.path;
        ae.size         = e.size;
        ae.packedSize  = e.size;
        ae.isDirectory = e.isDir;
        if (e.mtime != 0)
            ae.modified = std::chrono::system_clock::from_time_t(e.mtime);
        m_entries.push_back(std::move(ae));
    }

    // Append non-data tracks (audio) as raw playable entries alongside the filesystem
    for (const auto& t : m_tracks)
    {
        if (&t == dataTrack) continue;
        ArchiveEntry ae;
        ae.name = t.name;
        ae.path = t.name;
        ae.size = entrySizeForTrack(t.size, t.type);
        ae.packedSize = 0;
        ae.isDirectory = false;
        ae.compressionMethod = t.type;
        m_entries.push_back(std::move(ae));
    }

    LOG_DBG("CHD: mounted ISO 9660 filesystem, %zu entries", m_entries.size());
    return true;
}

void ChdEngine::buildCueSheet()
{
    m_cueSheetName.clear();
    m_cueSheetText.clear();

    // Only meaningful for CD-ROM track layouts (not HDD/DVD/RAW blobs)
    if (m_tracks.empty() || !m_tracks[0].isCdTrack)
        return;

    std::ostringstream cue;
    for (const auto& t : m_tracks)
    {
        std::string mode;
        if (t.type == "AUDIO")
            mode = "AUDIO";
        else
        {
            bool isMode2 = t.type.rfind("MODE2", 0) == 0;
            mode = (isMode2 ? "MODE2/" : "MODE1/") + std::to_string(t.userSize);
        }

        char trackHdr[32];
        std::snprintf(trackHdr, sizeof(trackHdr), "%02d", t.number > 0 ? t.number : 1);

        cue << "FILE \"" << t.name << "\" " << (t.type == "AUDIO" ? "WAVE" : "BINARY") << "\n";
        cue << "  TRACK " << trackHdr << " " << mode << "\n";
        cue << "    INDEX 01 00:00:00\n";
    }

    m_cueSheetText = cue.str();
    m_cueSheetName = fs::path(m_path).stem().string() + ".cue";
}

std::vector<uint8_t> ChdEngine::readRange(uint64_t offset, uint64_t length)
{
    if (!m_chd || m_hunkBytes == 0 || length == 0)
        return {};

    std::vector<uint8_t> result(length);
    if (m_hunkCache.size() != m_hunkBytes) m_hunkCache.resize(m_hunkBytes);

    uint64_t bytesRead = 0;
    while (bytesRead < length)
    {
        if (m_extractCancelled) { LOG_DBG("CHD: extract cancelled"); return {}; }
        uint64_t absPos = offset + bytesRead;
        uint32_t hunkNum = static_cast<uint32_t>(absPos / m_hunkBytes);
        uint32_t hunkOff = static_cast<uint32_t>(absPos % m_hunkBytes);

        if (hunkNum != m_cachedHunk)
        {
            chd_error err = chd_read(m_chd, hunkNum, m_hunkCache.data());
            if (err != CHDERR_NONE)
            {
                LOG_WARN("CHD: error reading hunk %u: %s", hunkNum, chd_error_string(err));
                result.resize(bytesRead);
                return result;
            }
            m_cachedHunk = hunkNum;
        }

        uint64_t toCopy = std::min<uint64_t>(m_hunkBytes - hunkOff, length - bytesRead);
        std::memcpy(result.data() + bytesRead, m_hunkCache.data() + hunkOff, toCopy);
        bytesRead += toCopy;
    }

    return result;
}

std::vector<uint8_t> ChdEngine::readTrackData(const TrackInfo& t, uint64_t trackPos, uint64_t length)
{
    if (!t.isCdTrack)
        return readRange(t.offset + trackPos, length);

    std::vector<uint8_t> result;
    result.reserve(length);
    uint64_t remaining = length;
    uint64_t pos = trackPos;

    while (remaining > 0)
    {
        if (m_extractCancelled) break;

        uint32_t frameIdx = static_cast<uint32_t>(pos / t.userSize);
        uint32_t frameOff = static_cast<uint32_t>(pos % t.userSize);
        uint64_t absFrame = t.startFrame + frameIdx;
        uint64_t hunkOff = absFrame * kCdFrameBytes + t.headerOff + frameOff;
        uint64_t chunk = std::min<uint64_t>(t.userSize - frameOff, remaining);

        auto data = readRange(hunkOff, chunk);
        if (data.empty()) break;
        result.insert(result.end(), data.begin(), data.end());
        pos += data.size();
        remaining -= data.size();
        if (data.size() < chunk) break;
    }
    return result;
}

bool ChdEngine::extractTrackData(const TrackInfo& t, std::ofstream& out)
{
    static constexpr uint64_t kChunk = 1u << 20; // 1 MB
    uint64_t remaining = t.size;
    uint64_t pos = 0;
    bool isAudio = t.type == "AUDIO";

    while (remaining > 0)
    {
        if (m_extractCancelled) return false;
        uint64_t want = std::min<uint64_t>(remaining, kChunk);
        auto data = readTrackData(t, pos, want);
        if (data.empty()) return false;
        if (isAudio) swapAudioSamples(data);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
        pos += data.size();
        remaining -= data.size();
        if (data.size() < want) break;
    }
    return remaining == 0;
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
    m_hasFilesystem = false;
    m_iso = Iso9660Reader{};
    m_cueSheetName.clear();
    m_cueSheetText.clear();
    m_cachedHunk = UINT32_MAX;
    m_hunkCache.clear();
}

const std::vector<ArchiveEntry>& ChdEngine::ListContents()
{
    return m_entries;
}

std::vector<uint8_t> ChdEngine::ReadFile(std::string_view entryName)
{
    if (m_hasFilesystem)
    {
        for (const auto& e : m_iso.entries())
        {
            if (e.isDir || e.path != entryName) continue;
            std::vector<uint8_t> result;
            result.reserve(e.size);
            m_iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                result.insert(result.end(), data, data + len);
                return true;
            });
            return result;
        }
    }

    if (!m_cueSheetName.empty() && entryName == m_cueSheetName)
        return std::vector<uint8_t>(m_cueSheetText.begin(), m_cueSheetText.end());

    for (const auto& t : m_tracks)
    {
        if (t.name != entryName) continue;

        auto pcm = readTrackData(t, 0, t.size);
        if (t.type != "AUDIO") return pcm;

        swapAudioSamples(pcm);
        WavHeader hdr = buildCdAudioWavHeader(static_cast<uint32_t>(pcm.size()));
        std::vector<uint8_t> result(sizeof(WavHeader) + pcm.size());
        std::memcpy(result.data(), &hdr, sizeof(WavHeader));
        std::memcpy(result.data() + sizeof(WavHeader), pcm.data(), pcm.size());
        return result;
    }
    LOG_WARN("CHD: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

bool ChdEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_chd || m_hunkBytes == 0) return false;
    m_extractCancelled = false;

    if (m_hasFilesystem)
    {
        for (const auto& e : m_iso.entries())
        {
            if (e.isDir || e.path != entryName) continue;

            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());
            std::ofstream out(dest, std::ios::binary);
            if (!out) { LOG_ERR("CHD: cannot create %s", dest.string().c_str()); return false; }

            return m_iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
                return out.good() && !m_extractCancelled.load();
            });
        }
    }

    if (!m_cueSheetName.empty() && entryName == m_cueSheetName)
    {
        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out) { LOG_ERR("CHD: cannot create %s", dest.string().c_str()); return false; }
        out.write(m_cueSheetText.data(), static_cast<std::streamsize>(m_cueSheetText.size()));
        return out.good();
    }

    const TrackInfo* track = nullptr;
    for (const auto& t : m_tracks)
        if (t.name == entryName) { track = &t; break; }
    if (!track) { LOG_WARN("CHD: entry '%.*s' not found", (int)entryName.size(), entryName.data()); return false; }

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) { LOG_ERR("CHD: cannot create %s", dest.string().c_str()); return false; }

    if (track->type == "AUDIO")
    {
        WavHeader hdr = buildCdAudioWavHeader(static_cast<uint32_t>(track->size));
        out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        if (!out) return false;
    }

    return extractTrackData(*track, out) && out.good();
}

bool ChdEngine::ExtractAll(std::string_view destPath)
{
    m_extractCancelled = false;

    for (const auto& ae : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("CHD: extract cancelled"); return false; }
        if (!isSafeEntryName(ae.name)) { LOG_WARN("CHD: skipping unsafe entry '%s'", ae.name.c_str()); continue; }

        if (ae.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / ae.path);
            continue;
        }

        fs::path outPath = fs::path(destPath) / ae.name;
        if (!Extract(ae.name, outPath.string()))
        {
            LOG_ERR("CHD: failed to extract %s", ae.name.c_str());
            return false;
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
