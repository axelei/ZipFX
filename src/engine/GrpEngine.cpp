#include "GrpEngine.h"

#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

bool GrpEngine::Open(std::string_view path)
{
    return parse(path, "GRP", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        uint8_t hdr[16];
        f.read(reinterpret_cast<char*>(hdr), 16);
        if (!f) return false;
        if (std::memcmp(hdr, "KenSilverman", 12) != 0) return false;

        int count = static_cast<int>(read32(hdr + 12));

        // Build engine computes offsets sequentially — the stored offset
        // field is unreliable. Data starts right after the directory.
        uint32_t dataOff = 16 + count * 20;

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[20];
            f.read(reinterpret_cast<char*>(de), 20);
            if (!f) return false;

            // GRP entry: name[12], size[4], offset[4](unused)
            char name[13] = {};
            std::memcpy(name, de, 12);
            name[12] = '\0';

            uint32_t sz = read32(de + 12);
            if (sz == 0) continue;

            entries.push_back({name, dataOff, sz});
            dataOff += sz;
        }
        return true;
    });
}
