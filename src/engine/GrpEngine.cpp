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

        // GRP entries are name[12] + size[4] = 16 bytes each.
        // There is NO stored offset field — positions are computed.
        uint32_t dataOff = 16 + count * 16;

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[16];
            f.read(reinterpret_cast<char*>(de), 16);
            if (!f) return false;

            char name[13] = {};
            std::memcpy(name, de, 12);
            name[12] = '\0';

            uint32_t sz = read32(de + 12);
            if (sz == 0) continue;

            entries.push_back({name, dataOff, sz});
            dataOff += sz;
        }
        return !entries.empty();
    });
}
