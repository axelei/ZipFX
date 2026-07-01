#include "PakEngine.h"
#include "BinaryUtils.h"

#include <algorithm>
#include <cstring>

bool PakEngine::Open(std::string_view path)
{
    return parse(path, "PAK", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;
        if (std::memcmp(hdr, "PACK", 4) != 0) return false;

        uint32_t dirOff = readLE32(hdr + 4);
        uint32_t dirSz = readLE32(hdr + 8);
        int count = static_cast<int>(dirSz / 64);

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[64];
            f.seekg(static_cast<std::streamoff>(dirOff) + static_cast<std::streamoff>(i) * 64);
            f.read(reinterpret_cast<char*>(de), 64);
            if (!f) return false;

            char name[57] = {};
            std::memcpy(name, de, 56);
            name[56] = '\0';

            uint32_t off = readLE32(de + 56);
            uint32_t sz = readLE32(de + 60);
            if (sz == 0) continue;

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
    writeLE32(f, 0);
    writeLE32(f, 0);

    std::vector<uint32_t> offsets;
    uint32_t curOff = 12;

    for (const auto& e : m_entries)
    {
        if (e.size == 0) { offsets.push_back(0); continue; }
        offsets.push_back(curOff);
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
        curOff += e.size;
    }

    // Write directory (name[56] + offset[4] + size[4])
    uint32_t dirOff = curOff;
    for (size_t i = 0; i < count; ++i)
    {
        if (m_entries[i].size == 0) continue;
        char name[56] = {};
        std::memcpy(name, m_entries[i].name.c_str(),
                    (std::min)(m_entries[i].name.size(), size_t{56}));
        f.write(name, 56);
        writeLE32(f, offsets[i]);
        writeLE32(f, m_entries[i].size);
    }

    // Fix header
    uint32_t dirSz = static_cast<uint32_t>(count * 64);
    f.seekp(4);
    writeLE32(f, dirOff);
    writeLE32(f, dirSz);
    return f.good();
}
