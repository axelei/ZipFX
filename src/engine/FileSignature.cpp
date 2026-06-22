#include "FileSignature.h"

#include <cstdio>
#include <string>
#include <vector>

ArchiveType FileSignature::Detect(std::string_view path)
{
    FILE* f = std::fopen(std::string(path).c_str(), "rb");
    if (!f) return ArchiveType::Unknown;

    uint8_t header[32];
    size_t n = std::fread(header, 1, sizeof(header), f);
    std::fclose(f);

    if (n < 4) return ArchiveType::Unknown;

    // ZIP: PK\x03\x04
    if (header[0] == 0x50 && header[1] == 0x4B &&
        header[2] == 0x03 && header[3] == 0x04)
        return ArchiveType::Zip;

    // 7z: 37 7A BC AF 27 1C
    if (n >= 6 &&
        header[0] == 0x37 && header[1] == 0x7A &&
        header[2] == 0xBC && header[3] == 0xAF &&
        header[4] == 0x27 && header[5] == 0x1C)
        return ArchiveType::SevenZip;

    // RAR or RAR5: Rar!\x1a\x07\x00 / \x01
    if (n >= 7 &&
        header[0] == 'R' && header[1] == 'a' &&
        header[2] == 'r' && header[3] == '!' &&
        header[4] == 0x1A && header[5] == 0x07)
    {
        if (header[6] == 0x00) return ArchiveType::Rar;
        if (header[6] == 0x01) return ArchiveType::Rar5;
        return ArchiveType::Rar;
    }

    // Gzip: 1F 8B
    if (n >= 2 && header[0] == 0x1F && header[1] == 0x8B)
        return ArchiveType::Gzip;

    return ArchiveType::Unknown;
}
