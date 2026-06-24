#include "WadEngine.h"

#include <algorithm>
#include <cstring>
#include <string>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

static void write32LE(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                     static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

bool WadEngine::Open(std::string_view path)
{
    auto detectedFmt = [](std::ifstream& f) -> std::string {
        uint8_t magic[4];
        f.read(reinterpret_cast<char*>(magic), 4);
        f.seekg(0);
        if (std::memcmp(magic, "IWAD", 4) == 0) return "IWAD";
        if (std::memcmp(magic, "PWAD", 4) == 0) return "PWAD";
        if (std::memcmp(magic, "WAD2", 4) == 0) return "WAD2";
        if (std::memcmp(magic, "WAD3", 4) == 0) return "WAD3";
        return {};
    };

    // Quick scan for format name
    std::ifstream probe(std::string(path), std::ios::binary);
    if (!probe) return false;
    auto fmt = detectedFmt(probe);
    probe.close();
    if (fmt.empty()) return false;

    m_fmtName = fmt;

    // WAD3 (Half-Life) uses 32-byte directory entries with 16-byte names.
    // Doom WAD uses 16-byte entries with 8-byte names.
    bool isWad3 = (fmt == "WAD3");
    int entrySize = isWad3 ? 32 : 16;

    return parse(path, m_fmtName.c_str(), [isWad3, entrySize](std::ifstream& f, std::vector<FileEntry>& entries) {
        entries.clear();
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;

        int count = static_cast<int>(read32(hdr + 4));
        uint32_t dirOff = read32(hdr + 8);

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[32];
            f.seekg(dirOff + i * entrySize);
            f.read(reinterpret_cast<char*>(de), entrySize);
            if (!f) return false;

            uint32_t off = read32(de);
            uint32_t sz  = isWad3 ? read32(de + 8) : read32(de + 4);
            if (sz == 0) continue;

            int nameLen = isWad3 ? 16 : 8;
            int nameOff = isWad3 ? 16 : 8;
            char name[17] = {};
            std::memcpy(name, de + nameOff, nameLen);
            // Trim trailing spaces and nulls
            for (int c = nameLen - 1; c >= 0 && (name[c] == ' ' || name[c] == '\0'); --c)
                name[c] = '\0';

            entries.push_back({name, off, sz});
        }
        return true;
    });
}

bool WadEngine::doSave(std::ofstream& f)
{
    // WAD: header[12] + data[varies] + directory[count*16]
    auto fmt = FormatName();

    // Write header (placeholder dirOff)
    f.write(fmt.data(), 4);
    write32LE(f, static_cast<uint32_t>(m_entries.size()));
    write32LE(f, 0);

    // Data offsets start at 12 (after header)
    std::vector<uint32_t> offsets;
    uint32_t curOff = 12;

    for (const auto& e : m_entries)
    {
        if (e.size == 0) { offsets.push_back(0); continue; }
        offsets.push_back(curOff);
        f.write(reinterpret_cast<const char*>(e.data.data()), e.size);
        curOff += e.size;
    }

    // Write directory
    uint32_t dirOff = curOff;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].size == 0) continue;
        write32LE(f, offsets[i]);
        write32LE(f, m_entries[i].size);
        char name[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
        std::memcpy(name, m_entries[i].name.c_str(),
                    (std::min)(m_entries[i].name.size(), size_t{8}));
        f.write(name, 8);
    }

    // Fix dirOff in header
    f.seekp(8);
    write32LE(f, dirOff);
    return f.good();
}
