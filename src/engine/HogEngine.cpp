#include "HogEngine.h"

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
            for (int c = 35; c >= 0 && name[c] == ' '; --c) name[c] = '\0';

            entries.push_back({name, off, sz});
        }
        return !entries.empty();
    });
}

bool HogEngine::doSave(std::ofstream& f)
{
    // HOG: magic[4] + entries[count*44] + data
    size_t count = m_entries.size();

    f.write("HOG\xF0", 4);

    // Compute offsets: entries start at 4, data starts at 4 + count*44
    uint32_t dataOff = 4 + static_cast<uint32_t>(count) * 44;

    // Write entries with computed data offsets
    for (size_t i = 0; i < count; ++i)
    {
        auto& e = m_entries[i];
        char name[36] = {};
        std::memcpy(name, e.name.c_str(),
                    (std::min)(e.name.size(), size_t{36}));
        f.write(name, 36);
        write32LE(f, dataOff);
        write32LE(f, e.size);
        dataOff += e.size;
    }

    // Write data
    for (auto& e : m_entries)
    {
        if (e.size > 0)
            f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
    }

    return f.good();
}
