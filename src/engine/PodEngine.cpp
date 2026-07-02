#include "PodEngine.h"
#include "BinaryUtils.h"
#include "Logging.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

bool PodEngine::Open(std::string_view path)
{
    return parse(path, "POD", [this](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[8];
        f.read(reinterpret_cast<char*>(hdr), 8);
        if (!f) return false;
        if (std::memcmp(hdr, "POD", 3) != 0) return false;
        char ver = static_cast<char>(hdr[3]);
        if (ver != '1' && ver != '2' && ver != '3' && ver != '5')
            return false;

        uint32_t count = readLE32(hdr + 4);

        // POD2/POD3 have an 80-byte comment after the header
        if (ver == '2' || ver == '3' || ver == '5')
        {
            char commentBuf[80] = {};
            f.read(commentBuf, 80);
            size_t clen = strnlen(commentBuf, 80);
            while (clen > 0 && commentBuf[clen - 1] == ' ') --clen;
            m_comment.assign(commentBuf, clen);
        }
        else
        {
            m_comment.clear();
        }

        // Directory entries: name[32] + size[4] + offset[4]
        for (uint32_t i = 0; i < count; ++i)
        {
            uint8_t de[40];
            f.read(reinterpret_cast<char*>(de), 40);
            if (!f) return false;

            char name[33] = {};
            std::memcpy(name, de, 32);
            name[32] = '\0';
            size_t len = std::strlen(name);
            while (len > 0 && name[len - 1] == ' ') --len;
            name[len] = '\0';

            uint32_t sz  = readLE32(de + 32);
            uint32_t off = readLE32(de + 36);

            if (sz == 0) continue;
            entries.push_back({name, off, sz});
        }
        return true;
    });
}

bool PodEngine::doSave(std::ofstream& f)
{
    // Write as POD1 (simplest variant)
    size_t count = m_entries.size();

    uint64_t dataOff64 = 8ull + static_cast<uint64_t>(count) * 40ull;

    // Write directory entries (name[32] + size[4] + offset[4])
    std::vector<uint32_t> offsets;
    uint64_t curOff = dataOff64;
    for (const auto& e : m_entries)
    {
        uint32_t sz = e.size > 0 ? e.size : 0;
        if (curOff > UINT32_MAX)
        {
            LOG_ERR("PodEngine: archive exceeds 4 GB, which the POD format cannot address");
            return false;
        }
        offsets.push_back(static_cast<uint32_t>(curOff));
        curOff += sz;
    }
    if (curOff > UINT32_MAX)
    {
        LOG_ERR("PodEngine: archive exceeds 4 GB, which the POD format cannot address");
        return false;
    }

    f.write("POD1", 4);
    writeLE32(f, static_cast<uint32_t>(count));

    for (size_t i = 0; i < count; ++i)
    {
        char name[32] = {};
        std::memcpy(name, m_entries[i].name.c_str(),
                    (std::min)(m_entries[i].name.size(), size_t{32}));
        f.write(name, 32);
        writeLE32(f, m_entries[i].size > 0 ? m_entries[i].size : 0);
        writeLE32(f, offsets[i]);
    }

    // Write data sequentially
    for (const auto& e : m_entries)
    {
        if (e.size == 0) continue;
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
    }

    return f.good();
}
