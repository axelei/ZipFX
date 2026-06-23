#include "PakEngine.h"

#include <algorithm>
#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

static void write32LE(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                     static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

bool PakEngine::Open(std::string_view path)
{
    return parse(path, "PAK", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;
        if (std::memcmp(hdr, "PACK", 4) != 0) return false;

        uint32_t dirOff = read32(hdr + 4);
        uint32_t dirSz = read32(hdr + 8);
        int count = static_cast<int>(dirSz / 64);

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[64];
            f.seekg(dirOff + i * 64);
            f.read(reinterpret_cast<char*>(de), 64);
            if (!f) return false;

            uint32_t off = read32(de);
            uint32_t sz = read32(de + 4);
            if (sz == 0) continue;

            char name[57] = {};
            std::memcpy(name, de + 8, 56);
            name[56] = '\0';

            entries.push_back({name, off, sz});
        }
        return true;
    });
}

bool PakEngine::doSave(std::ofstream& f)
{
    // PAK: header[12] + data[varies] + directory[count*64]
    size_t count = m_entries.size();

    // Write header (placeholder offsets)
    f.write("PACK", 4);
    write32LE(f, 0);
    write32LE(f, 0);

    std::vector<uint32_t> offsets;
    uint32_t curOff = 12;

    for (const auto& e : m_entries)
    {
        if (e.size == 0) { offsets.push_back(0); continue; }
        offsets.push_back(curOff);
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
        curOff += e.size;
    }

    // Write directory
    uint32_t dirOff = curOff;
    for (size_t i = 0; i < count; ++i)
    {
        if (m_entries[i].size == 0) continue;
        write32LE(f, offsets[i]);
        write32LE(f, m_entries[i].size);
        char name[56] = {};
        std::memcpy(name, m_entries[i].name.c_str(),
                    (std::min)(m_entries[i].name.size(), size_t{56}));
        f.write(name, 56);
    }

    // Fix header
    uint32_t dirSz = static_cast<uint32_t>(count * 64);
    f.seekp(4);
    write32LE(f, dirOff);
    write32LE(f, dirSz);
    return f.good();
}
