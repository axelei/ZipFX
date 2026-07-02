#include "CdiEngine.h"
#include "Logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

static const uint8_t kSyncHeader[12] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

// ── DiscJuggler CDI footer parsing ───────────────────────────────────────
//
// DiscJuggler .cdi files store raw track sector data sequentially at the
// start of the file, followed by a footer (session/track descriptor table)
// at the end. The footer format was reverse-engineered by the community;
// see e.g. https://problemkaputt.de/psxspx-cdrom-disk-images-cdi-discjuggler.htm
//
// Overall layout, forward from (filesize - footerSize):
//   Number of Sessions (1 byte)
//   for each session:
//     Session Block (15 bytes) — byte[1] = track count for this session
//     that many Track Blocks
//   Session Block for "no more sessions" (15 bytes, track count = 0)
//   Disc Info Block (variable)
//   Entrypoint (4 bytes, = footer size) at filesize-4
//
// Track Start Address in the Track Block is disc/TOC-relative addressing
// (e.g. GD-ROM's high-density area conventionally starts at LBA 45000) —
// NOT a file offset. Each track's actual file position is the cumulative
// byte length of every track stored before it (sectors are packed back to
// back with no gaps); this is verified against the footer's own start
// offset as a sanity check below.
namespace {

uint32_t rdU32(FILE* f) { uint8_t b[4] = {0}; std::fread(b, 1, 4, f); return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24); }
uint16_t rdU16(FILE* f) { uint8_t b[2] = {0}; std::fread(b, 1, 2, f); return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
uint8_t  rdU8(FILE* f)  { uint8_t b = 0; std::fread(&b, 1, 1, f); return b; }
void     skipBytes(FILE* f, long n) { if (n > 0) std::fseek(f, n, SEEK_CUR); }

// Track/Disc Header, shared by Track Blocks and the Disc Info Block.
// Consumes exactly 0x30+F bytes; returns F (filename length).
long parseCdiHeader(FILE* f, int& totalTracks, int& mediumType, std::string& fname)
{
    skipBytes(f, 0x0F);              // 12 + 3 unknown bytes
    totalTracks = rdU8(f);           // 0x0F
    uint8_t nameLen = rdU8(f);        // 0x10
    fname.resize(nameLen);
    if (nameLen) std::fread(fname.data(), 1, nameLen, f);
    skipBytes(f, 11);                 // 0x11+F
    skipBytes(f, 1);                  // 0x1C+F (unknown 02h)
    skipBytes(f, 10);                 // 0x1D+F
    skipBytes(f, 1);                  // 0x27+F (unknown 80h)
    skipBytes(f, 4);                  // 0x28+F (unknown 00057E40h)
    skipBytes(f, 2);                  // 0x2C+F
    mediumType = rdU16(f);            // 0x2E+F
    return nameLen;                   // cursor now at 0x30+F
}

} // namespace

bool CdiEngine::parseFooter()
{
    m_tracks.clear();

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long filesize = std::ftell(f);
    if (filesize < 1024) { std::fclose(f); return false; }

    std::fseek(f, -4, SEEK_END);
    uint32_t footerSize = rdU32(f);
    if (footerSize < 20 || static_cast<long>(footerSize) >= filesize)
    { std::fclose(f); return false; }

    long footerStart = filesize - static_cast<long>(footerSize);
    std::fseek(f, footerStart, SEEK_SET);
    uint8_t numSessions = rdU8(f);
    if (numSessions == 0 || numSessions > 99) { std::fclose(f); return false; }

    uint64_t cumulativeOffset = 0;
    bool ok = true;
    for (int s = 0; ok && s < numSessions; ++s)
    {
        // Session Block (15 bytes)
        rdU8(f);                       // 00h unknown
        uint8_t trackCount = rdU8(f);  // 01h track count
        skipBytes(f, 7);                // 02h unknown
        rdU8(f);                       // 09h unknown
        skipBytes(f, 3);                 // 0Ah unknown
        skipBytes(f, 2);                 // 0Dh unknown (FFh,FFh)

        if (trackCount == 0 || trackCount > 99) { ok = false; break; }

        for (int t = 0; ok && t < trackCount; ++t)
        {
            CdiTrackInfo tr;
            int totalTracks = 0, mediumType = 0;
            parseCdiHeader(f, totalTracks, mediumType, tr.name); // -> 0x30+F

            uint16_t numIndices = rdU16(f);       // 0x30+F -> 0x32+F
            skipBytes(f, static_cast<long>(numIndices) * 4);
            uint32_t cdTextLen = rdU32(f);        // -> 0x36+FI
            skipBytes(f, static_cast<long>(cdTextLen)); // -> 0x36+FIT

            skipBytes(f, 2);                        // -> 0x38+FIT
            tr.trackMode = rdU8(f);                 // -> 0x39+FIT
            skipBytes(f, 7);                         // -> 0x40+FIT
            tr.sessionNum = static_cast<int>(rdU32(f)); // -> 0x44+FIT
            tr.trackNum   = static_cast<int>(rdU32(f)); // -> 0x48+FIT
            tr.discStartLba = rdU32(f);             // -> 0x4C+FIT (disc/TOC-relative, not a file offset)
            tr.numSectors = rdU32(f);               // -> 0x50+FIT
            skipBytes(f, 0x0C);                       // -> 0x5C+FIT
            skipBytes(f, 4);                          // -> 0x60+FIT
            uint32_t readMode = rdU32(f);           // -> 0x64+FIT
            skipBytes(f, 4);                          // Control -> 0x68+FIT
            skipBytes(f, 1);                          // -> 0x69+FIT
            rdU32(f);                                // Track Length dup -> 0x6D+FIT
            skipBytes(f, 4);                          // -> 0x71+FIT
            skipBytes(f, 0x0C);                       // ISRC code -> 0x7D+FIT
            skipBytes(f, 4);                          // ISRC valid flag -> 0x81+FIT
            skipBytes(f, 1);                          // -> 0x82+FIT
            skipBytes(f, 8);                          // -> 0x8A+FIT
            skipBytes(f, 4);                          // -> 0x8E+FIT
            skipBytes(f, 4);                          // -> 0x92+FIT
            skipBytes(f, 4);                          // -> 0x96+FIT
            skipBytes(f, 4);                          // -> 0x9A+FIT
            skipBytes(f, 4);                          // -> 0x9E+FIT
            skipBytes(f, 0x2A);                       // -> 0xC8+FIT
            skipBytes(f, 4);                          // -> 0xCC+FIT
            skipBytes(f, 0x0C);                       // -> 0xD8+FIT
            skipBytes(f, 1);                          // session_type -> 0xD9+FIT
            skipBytes(f, 5);                          // -> 0xDE+FIT
            skipBytes(f, 1);                          // not-last-track flag -> 0xDF+FIT
            skipBytes(f, 1);                          // -> 0xE0+FIT
            skipBytes(f, 4);                          // address for last track -> 0xE4+FIT

            switch (readMode)
            {
            case 0: tr.sectorSize = 2048; tr.headerOff = 0;  tr.userSize = 2048; break; // Mode1
            case 1: tr.sectorSize = 2336; tr.headerOff = 8;  tr.userSize = 2048; break; // Mode2 Form1
            case 2: tr.sectorSize = 2352; tr.headerOff = 0;  tr.userSize = 2352; break; // Audio
            case 3: tr.sectorSize = 2368; tr.headerOff = 16; tr.userSize = 2048; break; // Raw+PQ
            case 4: tr.sectorSize = 2448; tr.headerOff = 16; tr.userSize = 2048; break; // Raw+PQRSTUVW
            default: ok = false; break;
            }
            if (!ok) break;

            tr.fileOffset = cumulativeOffset;
            cumulativeOffset += tr.numSectors * tr.sectorSize;
            m_tracks.push_back(tr);
        }
    }

    // Terminator session block (15 bytes, track count byte should be 0 but
    // don't hard-require it — just consume it and move on).
    if (ok) skipBytes(f, 15);

    bool valid = ok && !m_tracks.empty()
              && static_cast<long>(cumulativeOffset) == footerStart;
    std::fclose(f);

    if (!valid)
    {
        LOG_WARN("CDI: footer parse failed sanity check for %s (cumulative=%llu, footerStart=%ld) — falling back to legacy detection",
                  m_path.c_str(), (unsigned long long)cumulativeOffset, footerStart);
        m_tracks.clear();
        return false;
    }

    LOG_DBG("CDI: parsed %zu track(s) from footer in %s", m_tracks.size(), m_path.c_str());
    return true;
}

bool CdiEngine::tryMountFilesystems()
{
    bool anyMounted = false;
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        const auto& t = m_tracks[i];
        if (t.trackMode == 0) continue; // audio track, no filesystem to mount

        // The ISO 9660 structures inside some tracks (observed on GD-ROM-
        // style discs) reference LBAs that are disc-absolute (relative to
        // the track's own "Track Start Address" from the footer) rather
        // than relative to where the track's bytes actually sit in the
        // file. On top of that, a standard Red Book track physically
        // stores a 150-sector (2-second) pregap before its logical LBA 0 —
        // confirmed here empirically (VolSpaceSize was exactly numSectors
        // minus 150). So: file-relative sector = pregap + (lba -
        // discStartLba). Try the common pregap=150 case first, then 0 for
        // discs/rips that don't store one.
        Iso9660Reader iso;
        bool opened = false;
        for (uint32_t pregap : {150u, 0u})
        {
            auto sectorFn = [this, &t, pregap](uint32_t lba, uint8_t* out2048) -> bool {
                uint32_t relLba = pregap + (lba - t.discStartLba);
                auto data = readTrackRange(t, static_cast<uint64_t>(relLba) * t.userSize, t.userSize);
                if (data.size() != t.userSize) return false;
                std::memcpy(out2048, data.data(), t.userSize);
                return true;
            };
            // Iso9660Reader's scan requires the exact starting LBA — it
            // stops at the first sector that isn't a valid Volume
            // Descriptor, it doesn't scan forward looking for one.
            if (iso.open(sectorFn, t.discStartLba + 16) && !iso.entries().empty())
            { opened = true; break; }
        }
        if (!opened) continue;

        m_isoMounts.push_back(std::move(iso));
        m_isoMountTrackIdx.push_back(static_cast<int>(i));
        anyMounted = true;
    }

    // Prefix with "Track NN/" only when more than one filesystem mounted,
    // matching ChdEngine's convention for multi-filesystem discs.
    bool multi = m_isoMounts.size() > 1;
    m_isoMountPrefixes.clear();
    for (size_t m = 0; m < m_isoMounts.size(); ++m)
    {
        int ti = m_isoMountTrackIdx[m];
        std::string prefix;
        if (multi)
        {
            char buf[32];
            // trackNum is per-session (each session's tracks start over at
            // 0), so it's not globally unique across sessions — use the
            // track's position in the overall file order instead.
            std::snprintf(buf, sizeof(buf), "Track %02d/", ti + 1);
            prefix = buf;
        }
        m_isoMountPrefixes.push_back(prefix);

        for (const auto& e : m_isoMounts[m].entries())
        {
            ArchiveEntry ae;
            ae.name = prefix + e.path;
            ae.path = ae.name;
            ae.size = e.size;
            ae.packedSize = e.size;
            ae.isDirectory = e.isDir;
            ae.permissions = e.isDir ? 0755 : 0644;
            ae.modified = std::chrono::system_clock::from_time_t(e.mtime);
            m_entries.push_back(std::move(ae));
        }
    }

    // Expose any track that didn't yield a filesystem (audio tracks, or a
    // data track that failed to mount) as a raw entry so nothing vanishes.
    std::vector<bool> mounted(m_tracks.size(), false);
    for (int ti : m_isoMountTrackIdx) mounted[ti] = true;
    for (size_t i = 0; i < m_tracks.size(); ++i)
    {
        if (mounted[i]) continue;
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.bin", static_cast<int>(i) + 1);
        ArchiveEntry ae;
        ae.name = nameBuf;
        ae.path = ae.name;
        ae.size = m_tracks[i].numSectors * m_tracks[i].userSize;
        ae.packedSize = ae.size;
        ae.isDirectory = false;
        ae.compressionMethod = (m_tracks[i].trackMode == 0) ? "AUDIO" : "DATA";
        m_entries.push_back(std::move(ae));
    }

    m_hasFilesystem = anyMounted;
    return anyMounted;
}

std::vector<uint8_t> CdiEngine::readTrackRange(const CdiTrackInfo& t, uint64_t pos, uint64_t len) const
{
    std::vector<uint8_t> result;
    result.reserve(len);

    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return result;

    uint64_t remaining = len;
    uint64_t cur = pos;
    std::vector<uint8_t> sectorBuf(t.sectorSize);
    while (remaining > 0)
    {
        uint64_t sectorIdx = cur / t.userSize;
        uint64_t sectorOff = cur % t.userSize;
        uint64_t fileOff = t.fileOffset + sectorIdx * t.sectorSize + t.headerOff + sectorOff;

        uint64_t chunk = std::min<uint64_t>(t.userSize - sectorOff, remaining);

        if (fseeko(f, static_cast<off_t>(fileOff), SEEK_SET) != 0) break;
        size_t n = std::fread(sectorBuf.data(), 1, static_cast<size_t>(chunk), f);
        if (n == 0) break;
        result.insert(result.end(), sectorBuf.begin(), sectorBuf.begin() + n);
        cur += n;
        remaining -= n;
        if (n < chunk) break;
    }

    std::fclose(f);
    return result;
}

// ── Libarchive callbacks ─────────────────────────────────────────────────

static int cdiOpenCb(struct archive*, void* client_data)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    s->isoPos = 0;
    if (s->file)
        std::fseek(s->file, 0, SEEK_SET);
    return ARCHIVE_OK;
}

static la_ssize_t cdiReadCb(struct archive*, void* client_data, const void** buffer)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    if (!s->file) return ARCHIVE_FATAL;

    uint64_t isoBytes = s->totalSectors * 2048;
    if (s->isoPos >= isoBytes) return 0;

    size_t toRead = static_cast<size_t>(std::min<uint64_t>(isoBytes - s->isoPos, 262144));
    s->buf.resize(toRead);
    size_t bytesRead = 0;

    while (bytesRead < toRead)
    {
        uint64_t isoOff = s->isoPos + bytesRead;
        uint64_t sectorIdx = isoOff / 2048;
        uint64_t sectorOff = isoOff % 2048;
        uint64_t cdiOff = sectorIdx * s->sectorSize + s->seekHeader + sectorOff;

        size_t inSector = static_cast<size_t>(std::min<uint64_t>(2048 - sectorOff, toRead - bytesRead));

        if (fseeko(s->file, static_cast<off_t>(cdiOff), SEEK_SET) != 0)
            break;
        size_t n = std::fread(s->buf.data() + bytesRead, 1, inSector, s->file);
        if (n == 0)
            break;
        bytesRead += n;
        if (n < inSector)
            break;
    }

    s->buf.resize(bytesRead);
    s->isoPos += bytesRead;
    *buffer = s->buf.data();
    return static_cast<la_ssize_t>(bytesRead);
}

static la_int64_t cdiSeekCb(struct archive*, void* client_data,
                            la_int64_t offset, int whence)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    uint64_t isoBytes = s->totalSectors * 2048;

    la_int64_t newPos = 0;
    switch (whence)
    {
    case SEEK_SET: newPos = offset; break;
    case SEEK_CUR: newPos = static_cast<la_int64_t>(s->isoPos) + offset; break;
    case SEEK_END: newPos = static_cast<la_int64_t>(isoBytes) + offset; break;
    default:       return ARCHIVE_FATAL;
    }

    s->isoPos = static_cast<uint64_t>(std::max<la_int64_t>(0, std::min<la_int64_t>(newPos, static_cast<la_int64_t>(isoBytes))));
    return static_cast<la_int64_t>(s->isoPos);
}

static int cdiCloseCb(struct archive*, void* client_data)
{
    auto* s = static_cast<CdiIsoStream*>(client_data);
    if (s->file)
    {
        std::fclose(s->file);
        s->file = nullptr;
    }
    s->buf.clear();
    s->buf.shrink_to_fit();
    return ARCHIVE_OK;
}

// ── CdiEngine ────────────────────────────────────────────────────────────

CdiEngine::~CdiEngine()
{
    CdiEngine::Close();
}

bool CdiEngine::detectType()
{
    FILE* f = std::fopen(m_path.c_str(), "rb");
    if (!f) return false;

    uint8_t buf[16];
    bool ok = false;

    if (std::fread(buf, 1, 16, f) == 16 &&
        std::memcmp(buf, kSyncHeader, 12) == 0)
    {
        m_seekHeader = 16;

        auto probeType = [&](uint32_t probeOffset, uint32_t secSize,
                             uint32_t ecc, Type type) -> bool
        {
            std::fseek(f, long(probeOffset), SEEK_SET);
            uint8_t probe[12];
            if (std::fread(probe, 1, 12, f) == 12 &&
                std::memcmp(probe, kSyncHeader, 12) == 0)
            {
                m_sectorSize = secSize;
                m_seekEcc = ecc;
                m_type = type;
                return true;
            }
            return false;
        };

        if (probeType(2352, 2352, 288, Type::Raw) ||
            probeType(2368, 2368, 304, Type::PQ) ||
            probeType(2448, 2448, 384, Type::CdG))
            ok = true;
    }
    else
    {
        m_seekHeader = 0;
        m_sectorSize = 2048;
        m_seekEcc = 0;
        m_type = Type::Normal;
        ok = true;
    }

    if (ok)
    {
        std::fseek(f, 0, SEEK_END);
        long fileLen = std::ftell(f);
        m_totalSectors = (fileLen > 0) ? uint64_t(fileLen) / m_sectorSize : 0;
    }

    std::fclose(f);
    return ok;
}

bool CdiEngine::openCdiFile()
{
    if (m_cdiFile) return true;
    m_cdiFile = std::fopen(m_path.c_str(), "rb");
    return m_cdiFile != nullptr;
}

void CdiEngine::closeCdiFile()
{
    if (m_cdiFile)
    {
        std::fclose(m_cdiFile);
        m_cdiFile = nullptr;
    }
}

bool CdiEngine::readStrippedSector(uint8_t* out2048)
{
    if (m_seekHeader > 0 && std::fseek(m_cdiFile, m_seekHeader, SEEK_CUR) != 0)
        return false;
    if (std::fread(out2048, 1, 2048, m_cdiFile) != 2048)
        return false;
    if (m_seekEcc > 0 && std::fseek(m_cdiFile, m_seekEcc, SEEK_CUR) != 0)
        return false;
    return true;
}

bool CdiEngine::streamOpen()
{
    m_stream.file = std::fopen(m_path.c_str(), "rb");
    if (!m_stream.file)
    {
        LOG_ERR("CDI: failed to open %s", m_path.c_str());
        return false;
    }

    m_stream.sectorSize = m_sectorSize;
    m_stream.seekHeader = m_seekHeader;
    m_stream.seekEcc = m_seekEcc;
    m_stream.totalSectors = m_totalSectors;
    m_stream.isoPos = 0;

    m_archive = archive_read_new();
    if (!m_archive) { cdiCloseCb(nullptr, &m_stream); return false; }

    archive_read_support_format_iso9660(m_archive);

    archive_read_set_open_callback(m_archive, cdiOpenCb);
    archive_read_set_read_callback(m_archive, cdiReadCb);
    archive_read_set_seek_callback(m_archive, cdiSeekCb);
    archive_read_set_close_callback(m_archive, cdiCloseCb);

    if (archive_read_open1(m_archive) != ARCHIVE_OK)
    {
        LOG_WARN("CDI: ISO-9660 parsing failed: %s — falling back to raw image",
                 archive_error_string(m_archive));
        archive_read_free(m_archive);
        m_archive = nullptr;
        cdiCloseCb(nullptr, &m_stream);
        return false;
    }

    return true;
}

bool CdiEngine::loadAllEntries()
{
    m_entries.clear();

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        ArchiveEntry ae;
        ae.name = archive_entry_pathname(entry);
        ae.path = ae.name;
        la_int64_t rawSize = archive_entry_size(entry);
        ae.size = rawSize > 0 ? static_cast<uint64_t>(rawSize) : 0;
        ae.packedSize = ae.size;
        ae.isDirectory = archive_entry_filetype(entry) == AE_IFDIR;
        ae.permissions = archive_entry_perm(entry) & 0xFFF;
        if (ae.permissions == 0)
            ae.permissions = ae.isDirectory ? 0755 : 0644;

        time_t mtime = archive_entry_mtime(entry);
        ae.modified = std::chrono::system_clock::from_time_t(mtime);

        m_entries.push_back(std::move(ae));
    }

    LOG_DBG("CDI: loaded %zu entries from ISO inside %s",
            m_entries.size(), m_path.c_str());
    return true;
}

bool CdiEngine::ensureArchiveOpen()
{
    if (m_archive || m_fallbackRaw || m_hasRealTracks) return true;

    // Preferred path: parse the real DiscJuggler footer (session/track
    // table) and mount ISO 9660 filesystems from the actual data track(s),
    // instead of guessing at a single whole-file sync pattern. Multi-track
    // CDIs (e.g. Dreamcast GD-ROM dumps, which have a low-density system
    // track plus the real high-density game-data track) need this — the
    // legacy path below only ever looks at byte 0 of the file, which is
    // never the real data track's start for these discs. Once the footer
    // parses, always use it (even if no track happened to mount a
    // filesystem, e.g. an audio-only disc) rather than falling through to
    // the legacy single-blob guess, which would be strictly less accurate.
    if (parseFooter())
    {
        m_hasRealTracks = true;
        tryMountFilesystems();
        return true;
    }

    if (m_type == Type::Normal)
    {
        // No CDI wrapping — parse the source file directly
        m_archive = archive_read_new();
        archive_read_support_format_iso9660(m_archive);
        if (archive_read_open_filename(m_archive, m_path.c_str(), 2048) != ARCHIVE_OK)
        {
            LOG_WARN("CDI: failed to open %s as ISO: %s — falling back to raw",
                     m_path.c_str(), archive_error_string(m_archive));
            archive_read_free(m_archive);
            m_archive = nullptr;
            m_fallbackRaw = true;
            ArchiveEntry ae;
            ae.name = "data.iso";
            ae.path = "data.iso";
            ae.size = m_totalSectors * 2048;
            ae.packedSize = ae.size;
            ae.isDirectory = false;
            ae.compressionMethod = "ISO";
            m_entries.push_back(std::move(ae));
            return true;
        }
        return loadAllEntries();
    }

    // Try ISO-9660 via custom callbacks
    if (!streamOpen())
    {
        // Fallback: present the stripped data as a single raw entry
        m_fallbackRaw = true;
        ArchiveEntry ae;
        ae.name = "data.iso";
        ae.path = "data.iso";
        ae.size = m_totalSectors * 2048;
        ae.packedSize = m_totalSectors * m_sectorSize;
        ae.isDirectory = false;
        switch (m_type)
        {
        case Type::Raw:  ae.compressionMethod = "Mode1/RAW";   break;
        case Type::PQ:   ae.compressionMethod = "Mode2/PQ";    break;
        case Type::CdG:  ae.compressionMethod = "CD+G";        break;
        default:         ae.compressionMethod = "ISO";          break;
        }
        m_entries.push_back(std::move(ae));
        LOG_DBG("CDI: raw fallback — single entry data.iso (%llu bytes)",
                (unsigned long long)ae.size);
        return true;
    }

    return loadAllEntries();
}

bool CdiEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    if (!detectType())
    {
        LOG_ERR("CDI: unable to detect format for %s", m_path.c_str());
        return false;
    }

    if (m_totalSectors == 0)
    {
        LOG_ERR("CDI: empty file %s", m_path.c_str());
        return false;
    }

    m_isOpen = true;
    LOG_DBG("CDI: opened %s (%llu sectors, type %d)",
            m_path.c_str(), (unsigned long long)m_totalSectors, (int)m_type);
    return true;
}

void CdiEngine::Close()
{
    if (m_archive)
    {
        archive_read_free(m_archive);
        m_archive = nullptr;
    }
    if (m_stream.file)
    {
        std::fclose(m_stream.file);
        m_stream.file = nullptr;
    }
    closeCdiFile();
    m_stream.buf.clear();
    m_stream.buf.shrink_to_fit();
    m_entries.clear();
    m_fallbackRaw = false;
    m_tracks.clear();
    m_hasRealTracks = false;
    m_hasFilesystem = false;
    m_isoMounts.clear();
    m_isoMountPrefixes.clear();
    m_isoMountTrackIdx.clear();
    m_isOpen = false;
    m_totalSectors = 0;
    m_path.clear();
    m_type = Type::Normal;
    m_sectorSize = 2048;
    m_seekHeader = 0;
    m_seekEcc = 0;
}

const std::vector<ArchiveEntry>& CdiEngine::ListContents()
{
    if (!m_isOpen) { static const std::vector<ArchiveEntry> empty; return empty; }
    if (!ensureArchiveOpen()) { static const std::vector<ArchiveEntry> empty; return empty; }
    return m_entries;
}

// ── Raw fallback reads (no libarchive) ───────────────────────────────────

static std::vector<uint8_t> readRawCdi(FILE* f, uint64_t totalSectors,
                                       uint32_t seekHeader, uint32_t seekEcc)
{
    std::vector<uint8_t> data(totalSectors * 2048);
    std::fseek(f, 0, SEEK_SET);

    for (uint64_t i = 0; i < totalSectors; ++i)
    {
        if (seekHeader > 0 && std::fseek(f, seekHeader, SEEK_CUR) != 0)
        {
            data.resize(i * 2048);
            return data;
        }
        if (std::fread(data.data() + i * 2048, 1, 2048, f) != 2048)
        {
            data.resize(i * 2048);
            return data;
        }
        if (seekEcc > 0 && std::fseek(f, seekEcc, SEEK_CUR) != 0)
        {
            data.resize((i + 1) * 2048);
            return data;
        }
    }
    return data;
}

// ── Extraction ───────────────────────────────────────────────────────────

bool CdiEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    m_extractCancelled = false;

    if (m_hasRealTracks)
    {
        for (size_t m = 0; m < m_isoMounts.size(); ++m)
        {
            const auto& prefix = m_isoMountPrefixes[m];
            if (entryName.substr(0, prefix.size()) != prefix) continue;
            std::string_view rel = entryName.substr(prefix.size());

            auto& iso = m_isoMounts[m];
            for (const auto& e : iso.entries())
            {
                if (e.isDir || e.path != rel) continue;
                fs::path dest(destPath);
                fs::create_directories(dest.parent_path());
                std::ofstream out(dest, std::ios::binary);
                if (!out) return false;
                return iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
                    return out.good() && !m_extractCancelled.load();
                });
            }
        }

        // Raw track fallback (e.g. an unmounted audio track's "Track NN.bin")
        for (size_t ti = 0; ti < m_tracks.size(); ++ti)
        {
            const auto& t = m_tracks[ti];
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.bin", static_cast<int>(ti) + 1);
            if (entryName != nameBuf) continue;

            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());
            std::ofstream out(dest, std::ios::binary);
            if (!out) return false;

            uint64_t total = t.numSectors * t.userSize;
            constexpr uint64_t kChunk = 1u << 20;
            uint64_t pos = 0;
            while (pos < total)
            {
                if (m_extractCancelled) return false;
                uint64_t want = std::min<uint64_t>(kChunk, total - pos);
                auto data = readTrackRange(t, pos, want);
                if (data.empty()) return false;
                out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!out) return false;
                pos += data.size();
            }
            return true;
        }

        LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
        return false;
    }

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso")
        {
            LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
            return false;
        }
        if (!openCdiFile()) return false;

        fs::path dest(destPath);
        fs::create_directories(dest.parent_path());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;

        uint8_t buf[2048];
        for (uint64_t i = 0; i < m_totalSectors; ++i)
        {
            if (m_extractCancelled) { closeCdiFile(); return false; }
            if (!readStrippedSector(buf)) { closeCdiFile(); return false; }
            out.write(reinterpret_cast<const char*>(buf), 2048);
            if (!out) { closeCdiFile(); return false; }
        }
        closeCdiFile();
        return true;
    }

    // ISO mode: re-open and scan for the entry
    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return false;

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            fs::path dest(destPath);
            fs::create_directories(dest.parent_path());
            std::ofstream out(dest, std::ios::binary);
            if (!out) return false;

            std::array<char, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
            {
                if (m_extractCancelled) return false;
                out.write(buf.data(), bytesRead);
            }
            return bytesRead >= 0 && out.good();
        }
        archive_read_data_skip(m_archive);
    }

    LOG_WARN("CDI: entry '%s' not found", name.c_str());
    return false;
}

bool CdiEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    m_extractCancelled = false;

    if (m_hasRealTracks)
    {
        for (const auto& e : m_entries)
        {
            if (m_extractCancelled) return false;
            if (!isSafeEntryName(e.path)) { LOG_WARN("CdiEngine: skipping unsafe entry '%s'", e.path.c_str()); continue; }

            if (e.isDirectory)
            {
                fs::create_directories(fs::path(destPath) / e.path);
                continue;
            }
            fs::path outPath = fs::path(destPath) / e.path;
            fs::create_directories(outPath.parent_path());
            if (!Extract(e.path, outPath.string())) return false;
        }
        return true;
    }

    if (m_fallbackRaw)
    {
        std::string rawIsoDest = (fs::path(destPath) / "data.iso").string();
return Extract("data.iso", rawIsoDest);
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return false;

    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (m_extractCancelled) return false;

        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        std::string name(currentName);
        if (!isSafeEntryName(name)) { LOG_WARN("CdiEngine: skipping unsafe entry '%s'", name.c_str()); continue; }
        bool isDir = archive_entry_filetype(entry) == AE_IFDIR;

        fs::path fullPath = fs::path(destPath) / name;
        if (isDir) { fs::create_directories(fullPath); continue; }
        fs::create_directories(fullPath.parent_path());

        std::ofstream out(fullPath, std::ios::binary);
        if (!out) return false;

        std::array<char, 65536> buf;
        la_ssize_t bytesRead;
        while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
        {
            if (m_extractCancelled) return false;
            out.write(buf.data(), bytesRead);
        }
        if (bytesRead < 0) return false;
    }
    return true;
}

// ── Reading ──────────────────────────────────────────────────────────────

std::vector<uint8_t> CdiEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen) return {};
    if (!ensureArchiveOpen()) return {};

    m_extractCancelled = false;

    if (m_hasRealTracks)
    {
        for (size_t m = 0; m < m_isoMounts.size(); ++m)
        {
            const auto& prefix = m_isoMountPrefixes[m];
            if (entryName.substr(0, prefix.size()) != prefix) continue;
            std::string_view rel = entryName.substr(prefix.size());

            auto& iso = m_isoMounts[m];
            for (const auto& e : iso.entries())
            {
                if (e.isDir || e.path != rel) continue;
                std::vector<uint8_t> result;
                result.reserve(e.size);
                iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                    result.insert(result.end(), data, data + len);
                    return true;
                });
                return result;
            }
        }

        for (size_t ti = 0; ti < m_tracks.size(); ++ti)
        {
            const auto& t = m_tracks[ti];
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.bin", static_cast<int>(ti) + 1);
            if (entryName != nameBuf) continue;
            return readTrackRange(t, 0, t.numSectors * t.userSize);
        }

        LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
        return {};
    }

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso") return {};
        if (!openCdiFile()) return {};
        auto data = readRawCdi(m_cdiFile, m_totalSectors, m_seekHeader, m_seekEcc);
        closeCdiFile();
        return data;
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return {};

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            la_int64_t size = archive_entry_size(entry);
            if (size > 0)
            {
                std::vector<uint8_t> data(static_cast<size_t>(size));
                la_ssize_t bytesRead = archive_read_data(
                    m_archive, data.data(), static_cast<size_t>(size));
                if (bytesRead < 0) return {};
                data.resize(static_cast<size_t>(bytesRead));
                return data;
            }

            std::vector<uint8_t> data;
            std::array<uint8_t, 65536> buf;
            la_ssize_t bytesRead;
            while ((bytesRead = archive_read_data(m_archive, buf.data(), buf.size())) > 0)
                data.insert(data.end(), buf.begin(), buf.begin() + bytesRead);
            if (bytesRead < 0) return {};
            return data;
        }
        archive_read_data_skip(m_archive);
    }

    LOG_WARN("CDI: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

std::vector<uint8_t> CdiEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen) return {};
    if (!ensureArchiveOpen()) return {};

    m_extractCancelled = false;

    if (m_hasRealTracks)
    {
        for (size_t m = 0; m < m_isoMounts.size(); ++m)
        {
            const auto& prefix = m_isoMountPrefixes[m];
            if (entryName.substr(0, prefix.size()) != prefix) continue;
            std::string_view rel = entryName.substr(prefix.size());

            auto& iso = m_isoMounts[m];
            for (const auto& e : iso.entries())
            {
                if (e.isDir || e.path != rel) continue;
                std::vector<uint8_t> result;
                result.reserve(std::min<size_t>(e.size, maxBytes));
                iso.readData(e.lba, e.size, [&](const uint8_t* data, size_t len) -> bool {
                    size_t remaining = maxBytes - result.size();
                    size_t take = std::min(len, remaining);
                    result.insert(result.end(), data, data + take);
                    return result.size() < maxBytes;
                });
                return result;
            }
        }

        for (size_t ti = 0; ti < m_tracks.size(); ++ti)
        {
            const auto& t = m_tracks[ti];
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof(nameBuf), "Track %02d.bin", static_cast<int>(ti) + 1);
            if (entryName != nameBuf) continue;
            uint64_t total = t.numSectors * t.userSize;
            return readTrackRange(t, 0, std::min<uint64_t>(total, maxBytes));
        }

        return {};
    }

    if (m_fallbackRaw)
    {
        if (entryName != "data.iso") return {};
        if (!openCdiFile()) return {};

        uint64_t totalBytes = m_totalSectors * 2048;
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(totalBytes, maxBytes));
        std::vector<uint8_t> data(toRead);

        std::fseek(m_cdiFile, 0, SEEK_SET);
        size_t bytesRead = 0;
        uint8_t buf[2048];
        while (bytesRead < toRead)
        {
            if (!readStrippedSector(buf))
            {
                data.resize(bytesRead);
                break;
            }
            size_t chunk = std::min<size_t>(2048, toRead - bytesRead);
            std::memcpy(data.data() + bytesRead, buf, chunk);
            bytesRead += chunk;
        }

        closeCdiFile();
        data.resize(bytesRead);
        return data;
    }

    archive_read_free(m_archive);
    m_archive = nullptr;
    if (m_stream.file) { std::fclose(m_stream.file); m_stream.file = nullptr; }
    if (!streamOpen()) return {};

    std::string name(entryName);
    bool singleEntry = (m_entries.size() == 1);
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        const char* currentName = archive_entry_pathname(entry);
        if (!currentName) continue;

        if (name == currentName || (singleEntry && m_entries[0].name == name))
        {
            la_int64_t size = archive_entry_size(entry);
            size_t readSize = (size > 0)
                ? std::min(static_cast<size_t>(size), maxBytes) : maxBytes;

            std::vector<uint8_t> data(readSize);
            la_ssize_t bytesRead = archive_read_data(m_archive, data.data(), readSize);
            if (bytesRead < 0) return {};
            data.resize(static_cast<size_t>(bytesRead));
            return data;
        }
        archive_read_data_skip(m_archive);
    }
    return {};
}

bool CdiEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;
    if (!ensureArchiveOpen()) return false;

    if (m_hasRealTracks)
    {
        int total = static_cast<int>(m_entries.size());
        int current = 0;
        for (const auto& e : m_entries)
        {
            if (cancelFlag && cancelFlag()) return false;
            if (progressCallback) progressCallback(current++, total);
            if (e.isDirectory) continue;
            if (e.size > 0 && ReadFile(e.path).empty()) return false;
        }
        return true;
    }

    if (m_fallbackRaw)
    {
        if (!openCdiFile()) return false;
        uint8_t buf[2048];
        for (uint64_t i = 0; i < m_totalSectors; ++i)
        {
            if (cancelFlag && cancelFlag()) { closeCdiFile(); return false; }
            if (progressCallback)
                progressCallback(static_cast<int>(i), static_cast<int>(m_totalSectors));
            if (!readStrippedSector(buf)) { closeCdiFile(); return false; }
        }
        closeCdiFile();
        return true;
    }

    int total = static_cast<int>(m_entries.size());
    int current = 0;
    struct archive_entry* entry;
    while (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(current, total);
        if (archive_read_data_skip(m_archive) != ARCHIVE_OK) return false;
        current++;
    }
    return true;
}
