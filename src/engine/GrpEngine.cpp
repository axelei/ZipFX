#include "GrpEngine.h"
#include "BinaryUtils.h"

#include <algorithm>
#include <cstring>

bool GrpEngine::Open(std::string_view path)
{
    return parse(path, "GRP", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[16];
        f.read(reinterpret_cast<char*>(hdr), 16);
        if (!f) return false;
        if (std::memcmp(hdr, "KenSilverman", 12) != 0) return false;

        int count = static_cast<int>(readLE32(hdr + 12));
        if (count <= 0) return false;

        uint32_t dataOff = 16 + static_cast<uint32_t>(count) * 16;

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[16];
            f.read(reinterpret_cast<char*>(de), 16);
            if (!f) return false;

            char name[13] = {};
            std::memcpy(name, de, 12);
            name[12] = '\0';

            uint32_t sz = readLE32(de + 12);
            if (sz == 0) continue;

            entries.push_back({name, dataOff, sz});
            dataOff += sz;
        }
        return !entries.empty();
    });
}

bool GrpEngine::doSave(std::ofstream& f)
{
    // GRP: header[16] + entries[count*16] + data
    size_t count = m_entries.size();

    // Write header
    f.write("KenSilverman", 12);
    writeLE32(f, static_cast<uint32_t>(count));

    // Compute sizes and data offsets
    std::vector<uint32_t> sizes;
    sizes.reserve(count);
    uint32_t dataOff = 16 + static_cast<uint32_t>(count) * 16;

    for (const auto& e : m_entries)
    {
        uint32_t sz = (e.size > 0) ? e.size : 0;
        sizes.push_back(sz);
    }

    // Write directory entries (name[12] + size[4], no offset field)
    for (size_t i = 0; i < count; ++i)
    {
        char name[12] = {};
        std::memcpy(name, m_entries[i].name.c_str(),
                    (std::min)(m_entries[i].name.size(), size_t{12}));
        f.write(name, 12);
        writeLE32(f, sizes[i]);
    }

    // Write data sequentially
    for (size_t i = 0; i < count; ++i)
    {
        if (sizes[i] == 0) continue;
        f.write(reinterpret_cast<const char*>(m_entries[i].data.data()), sizes[i]);
    }

    return f.good();
}
