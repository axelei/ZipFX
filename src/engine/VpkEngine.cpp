#include "VpkEngine.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

static uint16_t read16(const uint8_t* d) {
    return static_cast<uint16_t>(d[0]) | (static_cast<uint16_t>(d[1]) << 8);
}

static std::string readNullString(std::ifstream& f)
{
    std::string s;
    char c;
    while (f.get(c) && c != '\0')
        s.push_back(c);
    return s;
}

bool VpkEngine::Open(std::string_view path)
{
    m_path = path;
    m_formatName = "VPK";

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    uint8_t hdr[12];
    f.read(reinterpret_cast<char*>(hdr), 12);
    if (!f) return false;
    if (read32(hdr) != 0x55AA1234) return false;

    uint32_t version = read32(hdr + 4);
    if (version != 1 && version != 2) return false;

    uint32_t treeSize = read32(hdr + 8);
    uint32_t headerSize = (version == 2) ? 28 : 12;
    uint32_t dataSectionStart = headerSize + treeSize;

    if (version == 2)
    {
        uint8_t v2hdr[16];
        f.read(reinterpret_cast<char*>(v2hdr), 16);
        if (!f) return false;

        uint32_t archiveMD5SectionSize = read32(v2hdr + 4);
        uint32_t otherMD5SectionSize = read32(v2hdr + 8);
        uint32_t signatureSectionSize = read32(v2hdr + 12);

        dataSectionStart = headerSize + treeSize
            + archiveMD5SectionSize + otherMD5SectionSize + signatureSectionSize;
    }

    f.seekg(headerSize);

    while (f)
    {
        std::string ext = readNullString(f);
        if (ext.empty() || !f) break;

        while (f)
        {
            std::string dir = readNullString(f);
            if (dir.empty() || !f) break;

            while (f)
            {
                std::string fname = readNullString(f);
                if (fname.empty() || !f) break;

                uint8_t entry[16];
                f.read(reinterpret_cast<char*>(entry), 16);
                if (!f) return false;

                uint16_t preloadSize = read16(entry + 4);
                uint16_t archiveIndex = read16(entry + 6);
                uint32_t entryOff = read32(entry + 8);
                uint32_t entryLen = read32(entry + 12);

                if (archiveIndex == 0x7FFF)
                {
                    std::string fullPath;
                    if (!dir.empty() && dir != " ")
                    {
                        fullPath = dir;
                        fullPath += '/';
                    }
                    fullPath += fname;
                    fullPath += '.';
                    fullPath += ext;

                    m_entries.push_back({
                        fullPath,
                        dataSectionStart + entryOff,
                        entryLen
                    });
                }

                if (preloadSize > 0)
                    f.seekg(preloadSize, std::ios::cur);
            }
        }
    }

    if (m_entries.empty()) return false;

    std::sort(m_entries.begin(), m_entries.end(),
        [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });

    m_isOpen = true;
    return true;
}
