#include "GobEngine.h"
#include "BinaryUtils.h"

#include <algorithm>
#include <cstring>

bool GobEngine::Open(std::string_view path)
{
    return parse(path, "GOB", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[8];
        f.read(reinterpret_cast<char*>(hdr), 8);
        if (!f) return false;
        if (std::memcmp(hdr, "GOB ", 4) != 0) return false;

        int count = static_cast<int>(readLE32(hdr + 4));

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[136];
            f.read(reinterpret_cast<char*>(de), 136);
            if (!f) return false;

            uint32_t off = readLE32(de);
            uint32_t sz  = readLE32(de + 4);

            char name[129] = {};
            std::memcpy(name, de + 8, 128);
            name[128] = '\0';
            // Trim trailing nulls/spaces
            size_t len = std::strlen(name);
            while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\0'))
                --len;
            name[len] = '\0';

            if (sz == 0) continue;
            entries.push_back({name, off, sz});
        }
        return true;
    });
}

bool GobEngine::doSave(std::ofstream& f)
{
    size_t count = m_entries.size();

    f.write("GOB ", 4);
    writeLE32(f, static_cast<uint32_t>(count));

    uint32_t dataOff = 8 + static_cast<uint32_t>(count) * 136;

    // Write directory entries (offset[4] + size[4] + name[128])
    for (const auto& e : m_entries)
    {
        uint32_t sz = e.size > 0 ? e.size : 0;
        writeLE32(f, dataOff);
        writeLE32(f, sz);

        char name[128] = {};
        std::memcpy(name, e.name.c_str(),
                    (std::min)(e.name.size(), size_t{128}));
        f.write(name, 128);

        dataOff += sz;
    }

    // Write data sequentially
    for (const auto& e : m_entries)
    {
        if (e.size == 0) continue;
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
    }

    return f.good();
}
