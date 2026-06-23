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

static void write32LE(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                     static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

static void write16LE(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
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
    m_entries.clear();

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

bool VpkEngine::doSave(std::ofstream& f)
{
    // Group entries by (ext, dir, fname) sorted
    struct Component {
        std::string ext, dir, fname;
        size_t idx;
        uint32_t size;
    };
    std::vector<Component> comps;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        auto& e = m_entries[i];
        auto dot = e.name.rfind('.');
        auto slash = e.name.rfind('/');
        std::string ext = (dot != std::string::npos) ? e.name.substr(dot + 1) : "";
        std::string dir = (slash != std::string::npos) ? e.name.substr(0, slash) : " ";
        std::string fname = e.name.substr(
            (slash != std::string::npos) ? slash + 1 : 0,
            (dot != std::string::npos) ? dot - ((slash != std::string::npos) ? slash + 1 : 0)
                                       : std::string::npos);
        comps.push_back({ext, dir, fname, i, m_entries[i].size});
    }
    std::sort(comps.begin(), comps.end(), [](auto& a, auto& b) {
        if (a.ext != b.ext) return a.ext < b.ext;
        if (a.dir != b.dir) return a.dir < b.dir;
        return a.fname < b.fname;
    });

    // Build directory tree in memory
    std::vector<uint8_t> tree;
    auto putStr = [&](const std::string& s) {
        tree.insert(tree.end(), s.begin(), s.end());
        tree.push_back(0);
    };
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) { tree.push_back(static_cast<uint8_t>(v)); v >>= 8; }
    };
    auto put16 = [&](uint16_t v) {
        tree.push_back(static_cast<uint8_t>(v));
        tree.push_back(static_cast<uint8_t>(v >> 8));
    };

    uint32_t dataOff = 0; // cumulative data offset (relative to data section start)

    size_t ci = 0;
    while (ci < comps.size())
    {
        putStr(comps[ci].ext);
        while (ci < comps.size())
        {
            auto& cur = comps[ci];
            putStr(cur.dir);
            while (ci < comps.size())
            {
                auto& c = comps[ci];
                if (c.ext != cur.ext) break;
                if (c.dir != cur.dir) break;

                putStr(c.fname);

                // CRC (0), preloadSize (0), archiveIndex (0x7FFF)
                put32(0);
                put16(0);
                put16(0x7FFF);
                // entryOffset (relative to data section start)
                put32(dataOff);
                // entryLength
                put32(c.size);

                dataOff += c.size;
                ++ci;
            }
            putStr({}); // end of files in this dir
            if (ci >= comps.size()) break;
            if (comps[ci].ext != comps[ci-1].ext) break;
        }
        putStr({}); // end of dirs for this ext
    }
    putStr({}); // end of exts

    // VPK version 1 header: signature + version + treeSize
    uint32_t version = 1;
    uint32_t treeSize = static_cast<uint32_t>(tree.size());
    uint32_t headerSize = 12;

    write32LE(f, 0x55AA1234);
    write32LE(f, version);
    write32LE(f, treeSize);

    // Write tree
    f.write(reinterpret_cast<const char*>(tree.data()), tree.size());

    // Write file data
    for (auto& e : m_entries)
    {
        if (e.size > 0)
            f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
    }

    return f.good();
}
