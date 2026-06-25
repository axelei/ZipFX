#include "BigEngine.h"
#include "BinaryUtils.h"

#include <algorithm>
#include <cstring>

bool BigEngine::Open(std::string_view path)
{
    return parse(path, "BIG", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;
        if ((std::memcmp(hdr, "BIGF", 4) != 0) &&
            (std::memcmp(hdr, "BIG4", 4) != 0))
            return false;

        uint32_t count = readBE32(hdr + 8);

        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t de[8];
            f.read(reinterpret_cast<char*>(de), 8);
            if (!f) return false;

            uint32_t off = readBE32(de);
            uint32_t sz  = readBE32(de + 4);

            // Read null-terminated filename
            std::string name;
            char c;
            while (f.get(c) && c != '\0')
                name += c;
            if (!f) return false;

            if (sz == 0) continue;
            entries.push_back({name, off, sz});
        }
        return true;
    });
}

bool BigEngine::doSave(std::ofstream& f)
{
    size_t count = m_entries.size();

    // Compute header + directory size to determine data start
    // Header: magic[4] + totalSize[4] + count[4]
    // Each entry: offset[4] + size[4] + name + null terminator
    uint32_t dirSize = 12;
    for (const auto& e : m_entries)
        dirSize += 8 + static_cast<uint32_t>(e.name.size()) + 1;

    // Write header with placeholder totalSize
    f.write("BIGF", 4);
    writeBE32(f, 0);
    writeBE32(f, static_cast<uint32_t>(count));

    // Compute offsets
    uint32_t dataOff = dirSize;
    std::vector<uint32_t> offsets;
    for (const auto& e : m_entries)
    {
        offsets.push_back(dataOff);
        dataOff += e.size > 0 ? e.size : 0;
    }

    // Write directory entries (offset[4] + size[4] + name + '\0')
    for (size_t i = 0; i < count; ++i)
    {
        writeBE32(f, offsets[i]);
        writeBE32(f, m_entries[i].size > 0 ? m_entries[i].size : 0);
        f.write(m_entries[i].name.c_str(), m_entries[i].name.size() + 1);
    }

    // Write data sequentially
    for (const auto& e : m_entries)
    {
        if (e.size == 0) continue;
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
    }

    // Fix totalSize in header
    uint32_t totalSize = dataOff;
    f.seekp(4);
    writeBE32(f, totalSize);
    return f.good();
}
