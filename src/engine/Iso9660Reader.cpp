#include "Iso9660Reader.h"

#include <algorithm>
#include <cstring>

// ── Little-endian helpers ─────────────────────────────────────────────────────

static uint32_t le32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint16_t be16(const uint8_t* p)
{
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

// ── Date decoding ─────────────────────────────────────────────────────────────

time_t Iso9660Reader::decodeDate(const uint8_t* d)
{
    // 7-byte directory record date:
    // [0] years since 1900, [1] month (1-12), [2] day, [3] hour, [4] min, [5] sec,
    // [6] timezone offset in 15-min units from GMT (signed)
    struct tm t{};
    t.tm_year = d[0];
    t.tm_mon  = d[1] - 1;
    t.tm_mday = d[2];
    t.tm_hour = d[3];
    t.tm_min  = d[4];
    t.tm_sec  = d[5];
    time_t raw = mktime(&t);
    // Adjust for GMT offset
    int off = static_cast<int8_t>(d[6]); // signed quarters of an hour
    raw -= off * 15 * 60;
    return raw;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool Iso9660Reader::open(SectorFn reader)
{
    m_reader = std::move(reader);
    m_entries.clear();
    m_opened = false;
    if (parseVolumeDescriptors())
    {
        m_opened = true;
        return true;
    }
    return false;
}

bool Iso9660Reader::readData(uint32_t lba, uint32_t size,
                              const std::function<bool(const uint8_t*, size_t)>& consumer) const
{
    uint8_t  sector[2048];
    uint32_t remaining = size;
    uint32_t curLba    = lba;

    while (remaining > 0)
    {
        if (!m_reader(curLba, sector)) return false;
        size_t chunk = std::min(remaining, 2048u);
        if (!consumer(sector, chunk)) return false;
        remaining -= static_cast<uint32_t>(chunk);
        ++curLba;
    }
    return true;
}

// ── Volume descriptor scanning ────────────────────────────────────────────────

bool Iso9660Reader::parseVolumeDescriptors()
{
    uint8_t  sector[2048];
    uint32_t pvdRootLba = 0, pvdRootSize = 0;
    uint32_t svdRootLba = 0, svdRootSize = 0;
    bool     havePvd = false, haveSvd = false;

    // VDs start at LBA 16; scan until terminator or an implausible run
    for (uint32_t lba = 16; lba < 64; ++lba)
    {
        if (!m_reader(lba, sector)) break;

        // All volume descriptors carry "CD001" at bytes 1-5
        if (std::memcmp(sector + 1, "CD001", 5) != 0) break;

        uint8_t vdType = sector[0];

        if (vdType == 0xFF) break; // Volume Descriptor Set Terminator

        if (vdType == 0x01) // Primary Volume Descriptor
        {
            // Root directory record starts at byte 156 of the PVD sector
            pvdRootLba  = le32(sector + 156 + 2);
            pvdRootSize = le32(sector + 156 + 10);
            havePvd     = true;
        }
        else if (vdType == 0x02) // Supplementary VD — check for Joliet escape sequences
        {
            // Joliet escape sequences are at bytes 88-90 of the SVD:
            //   {0x25, 0x2F, 0x40} = UCS-2 Level 1
            //   {0x25, 0x2F, 0x43} = UCS-2 Level 2
            //   {0x25, 0x2F, 0x45} = UCS-2 Level 3
            if (sector[88] == 0x25 && sector[89] == 0x2F
                && (sector[90] == 0x40 || sector[90] == 0x43 || sector[90] == 0x45))
            {
                svdRootLba  = le32(sector + 156 + 2);
                svdRootSize = le32(sector + 156 + 10);
                haveSvd     = true;
            }
        }
    }

    if (!havePvd) return false;

    // Prefer Joliet (Unicode names); fall back to PVD (ASCII)
    if (haveSvd)
        walkDir(svdRootLba, svdRootSize, "", true);
    else
        walkDir(pvdRootLba, pvdRootSize, "", false);

    return true;
}

// ── Directory tree walking ────────────────────────────────────────────────────

void Iso9660Reader::walkDir(uint32_t lba, uint32_t size,
                             const std::string& prefix, bool joliet)
{
    uint8_t  sector[2048];
    uint32_t remaining = size;
    uint32_t curLba    = lba;

    while (remaining > 0)
    {
        if (!m_reader(curLba, sector)) return;

        uint32_t sectorBytes = std::min(remaining, 2048u);
        uint32_t pos = 0;

        while (pos < sectorBytes)
        {
            uint8_t recLen = sector[pos];
            if (recLen == 0) break; // zero pad — skip rest of this sector

            if (pos + recLen > 2048) break; // malformed
            const uint8_t* rec = sector + pos;

            uint8_t idLen = rec[32];

            // Skip "." (0x00) and ".." (0x01) self/parent entries
            if (idLen == 1 && (rec[33] == 0x00 || rec[33] == 0x01))
            {
                pos += recLen;
                continue;
            }

            uint32_t fileLba  = le32(rec + 2);
            uint32_t fileSize = le32(rec + 10);
            uint8_t  flags    = rec[25];
            bool     isDir    = (flags & 0x02) != 0;
            time_t   mtime    = decodeDate(rec + 18);

            std::string name;
            name.reserve(idLen);

            if (joliet)
            {
                // File identifier is UCS-2 big-endian; encode to UTF-8
                for (uint8_t i = 0; i + 1 < idLen; i += 2)
                {
                    uint16_t ch = be16(rec + 33 + i);
                    if (ch < 0x0080)
                    {
                        name += static_cast<char>(ch);
                    }
                    else if (ch < 0x0800)
                    {
                        name += static_cast<char>(0xC0 | (ch >> 6));
                        name += static_cast<char>(0x80 | (ch & 0x3F));
                    }
                    else
                    {
                        name += static_cast<char>(0xE0 | (ch >> 12));
                        name += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                        name += static_cast<char>(0x80 | (ch & 0x3F));
                    }
                }
            }
            else
            {
                // Standard ISO 9660 ASCII; strip ";1" version suffix from files
                name.assign(reinterpret_cast<const char*>(rec + 33), idLen);
                if (!isDir)
                {
                    auto semi = name.rfind(';');
                    if (semi != std::string::npos) name.erase(semi);
                }
            }

            if (name.empty()) { pos += recLen; continue; }

            std::string fullPath = prefix.empty() ? name : (prefix + "/" + name);

            Entry e;
            e.path  = fullPath;
            e.lba   = fileLba;
            e.size  = fileSize;
            e.isDir = isDir;
            e.mtime = mtime;
            m_entries.push_back(e);

            if (isDir)
                walkDir(fileLba, fileSize, fullPath, joliet);

            pos += recLen;
        }

        ++curLba;
        remaining = (remaining > 2048) ? remaining - 2048 : 0;
    }
}
