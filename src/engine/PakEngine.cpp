#include "PakEngine.h"

#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
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
