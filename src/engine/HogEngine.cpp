#include "HogEngine.h"

#include <cstring>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

bool HogEngine::Open(std::string_view path)
{
    return parse(path, "HOG", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        uint8_t magic[4];
        f.read(reinterpret_cast<char*>(magic), 4);
        if (std::memcmp(magic, "HOG\xF0", 4) != 0) return false;

        while (true)
        {
            uint8_t de[44];
            f.read(reinterpret_cast<char*>(de), 44);
            if (!f || f.gcount() < 44) break;

            uint32_t off = read32(de + 36);
            uint32_t sz  = read32(de + 40);
            if (sz == 0 && off == 0) break;

            char name[37] = {};
            std::memcpy(name, de, 36);
            name[36] = '\0';
            // Trim trailing spaces
            for (int c = 35; c >= 0 && name[c] == ' '; --c) name[c] = '\0';

            entries.push_back({name, off, sz});
        }
        return !entries.empty();
    });
}
