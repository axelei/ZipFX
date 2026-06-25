#include "RffEngine.h"
#include "BinaryUtils.h"

#include <algorithm>
#include <cstring>

bool RffEngine::Open(std::string_view path)
{
    return parse(path, "RFF", [](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[16];
        f.read(reinterpret_cast<char*>(hdr), 16);
        if (!f) return false;
        if (std::memcmp(hdr, "RFF\x1A", 4) != 0) return false;

        uint32_t dirOff  = readLE32(hdr + 8);
        uint32_t count   = readLE32(hdr + 12);

        f.seekg(dirOff);
        if (!f) return false;

        for (uint32_t i = 0; i < count; ++i)
        {
            // RFF directory entry: 48 bytes
            // [16 unused][offset:4][size:4][4 unused][4 unused][1 flags][name:11][ext:3][1 unused]
            uint8_t de[48];
            f.read(reinterpret_cast<char*>(de), 48);
            if (!f) return false;

            uint32_t off = readLE32(de + 16);
            uint32_t sz  = readLE32(de + 20);

            char name[12] = {};
            std::memcpy(name, de + 33, 11);
            name[11] = '\0';

            char ext[4] = {};
            std::memcpy(ext, de + 44, 3);
            ext[3] = '\0';

            // Trim trailing spaces
            size_t nLen = std::strlen(name);
            while (nLen > 0 && name[nLen - 1] == ' ') --nLen;
            name[nLen] = '\0';
            size_t eLen = std::strlen(ext);
            while (eLen > 0 && ext[eLen - 1] == ' ') --eLen;
            ext[eLen] = '\0';

            std::string fullName(name);
            if (eLen > 0)
                fullName += std::string(".") + ext;

            if (sz == 0) continue;
            entries.push_back({fullName, off, sz});
        }
        return true;
    });
}

bool RffEngine::doSave(std::ofstream& f)
{
    // RFF: header[16] + data[varies] + directory[count*48]
    size_t count = m_entries.size();

    // Write header (magic + version + placeholder dirOffset + count)
    f.write("RFF\x1A", 4);
    writeLE32(f, 0x0301);  // version 3.1 (Blood)
    writeLE32(f, 0);       // dirOffset placeholder
    writeLE32(f, static_cast<uint32_t>(count));

    // Write data
    std::vector<uint32_t> offsets;
    uint32_t curOff = 16;
    for (const auto& e : m_entries)
    {
        if (e.size == 0) { offsets.push_back(0); continue; }
        offsets.push_back(curOff);
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
        curOff += e.size;
    }

    // Write directory
    uint32_t dirOff = curOff;
    for (size_t i = 0; i < count; ++i)
    {
        // Split name into base + extension
        std::string base = m_entries[i].name;
        std::string ext;
        auto dotPos = base.rfind('.');
        if (dotPos != std::string::npos)
        {
            ext = base.substr(dotPos + 1);
            base = base.substr(0, dotPos);
        }

        uint8_t de[48] = {};
        de[16] = static_cast<uint8_t>(offsets[i]);
        de[17] = static_cast<uint8_t>(offsets[i] >> 8);
        de[18] = static_cast<uint8_t>(offsets[i] >> 16);
        de[19] = static_cast<uint8_t>(offsets[i] >> 24);
        uint32_t sz = m_entries[i].size;
        de[20] = static_cast<uint8_t>(sz);
        de[21] = static_cast<uint8_t>(sz >> 8);
        de[22] = static_cast<uint8_t>(sz >> 16);
        de[23] = static_cast<uint8_t>(sz >> 24);
        std::memcpy(de + 33, base.c_str(), (std::min)(base.size(), size_t{11}));
        std::memcpy(de + 44, ext.c_str(), (std::min)(ext.size(), size_t{3}));
        f.write(reinterpret_cast<const char*>(de), 48);
    }

    // Fix dirOffset in header
    f.seekp(8);
    writeLE32(f, dirOff);
    return f.good();
}
