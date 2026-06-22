#include "WadEngine.h"

#include <cstring>
#include <string>

static uint32_t read32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
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

    return parse(path, m_fmtName.c_str(), [](std::ifstream& f, std::vector<FileEntry>& entries) {
        uint8_t hdr[12];
        f.read(reinterpret_cast<char*>(hdr), 12);
        if (!f) return false;

        int count = static_cast<int>(read32(hdr + 4));
        uint32_t dirOff = read32(hdr + 8);

        for (int i = 0; i < count; ++i)
        {
            uint8_t de[16];
            f.seekg(dirOff + i * 16);
            f.read(reinterpret_cast<char*>(de), 16);
            if (!f) return false;

            uint32_t off = read32(de);
            uint32_t sz  = read32(de + 4);
            if (sz == 0) continue;

            char name[9] = {};
            std::memcpy(name, de + 8, 8);
            for (int c = 7; c >= 0 && name[c] == ' '; --c) name[c] = '\0';

            entries.push_back({name, off, sz});
        }
        return true;
    });
}
