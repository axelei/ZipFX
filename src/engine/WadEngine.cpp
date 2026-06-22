#include "WadEngine.h"

#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

bool WadEngine::Open(std::string_view path)
{
    return parse(path, "WAD", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;
        if (std::memcmp(hdr, "IWAD", 4) != 0 && std::memcmp(hdr, "PWAD", 4) != 0)
            return false;

        int count = static_cast<int>(read32(hdr + 4));
        uint32_t dirOff = read32(hdr + 8);

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[16];
            f.seekg(dirOff + i * 16);
            f.read(reinterpret_cast<char*>(de), 16);
            if (!f) return false;

            uint32_t off = read32(de);
            uint32_t sz = read32(de + 4);
            if (sz == 0) continue;

            char name[9] = {};
            std::memcpy(name, de + 8, 8);
            for (int c = 7; c >= 0 && name[c] == ' '; --c) name[c] = '\0';

            entries.push_back({name, off, sz});
        }
        return true;
    });
}
