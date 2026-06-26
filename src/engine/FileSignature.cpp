#include "FileSignature.h"

#include <cstdio>
#include <string>

struct SigEntry
{
    ArchiveType type;
    size_t minBytes;
    bool (*match)(const uint8_t* data, size_t len);
};

static const SigEntry kSignatures[] =
{
    { ArchiveType::Zip,      4, [](const uint8_t* d, size_t) {
        return d[0] == 0x50 && d[1] == 0x4B &&
               d[2] == 0x03 && d[3] == 0x04; }},
    { ArchiveType::SevenZip, 6, [](const uint8_t* d, size_t) {
        return d[0] == 0x37 && d[1] == 0x7A &&
               d[2] == 0xBC && d[3] == 0xAF &&
               d[4] == 0x27 && d[5] == 0x1C; }},
    { ArchiveType::Rar,      7, [](const uint8_t* d, size_t) {
        return d[0] == 'R' && d[1] == 'a' && d[2] == 'r' &&
               d[3] == '!' && d[4] == 0x1A && d[5] == 0x07 &&
               d[6] == 0x00; }},
    { ArchiveType::Rar5,     7, [](const uint8_t* d, size_t) {
        return d[0] == 'R' && d[1] == 'a' && d[2] == 'r' &&
               d[3] == '!' && d[4] == 0x1A && d[5] == 0x07 &&
               d[6] == 0x01; }},
    { ArchiveType::Gzip,     2, [](const uint8_t* d, size_t) {
        return d[0] == 0x1F && d[1] == 0x8B; }},
    { ArchiveType::Cab,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'M' && d[1] == 'S' && d[2] == 'C' && d[3] == 'F'; }},
    { ArchiveType::Xar,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'x' && d[1] == 'a' && d[2] == 'r' && d[3] == '!'; }},
    { ArchiveType::Cpio,     6, [](const uint8_t* d, size_t) {
        return (d[0] == '0' && d[1] == '7' && d[2] == '0' && d[3] == '7' &&
                d[4] == '0' && d[5] == '7') ||
               (d[0] == '0' && d[1] == '7' && d[2] == '0' && d[3] == '7' &&
                d[4] == '0' && d[5] == '1'); }},
    { ArchiveType::Lha,      7, [](const uint8_t* d, size_t) {
        return d[2] == '-' && d[3] == 'l' && d[6] == '-'; }},
    { ArchiveType::Ar,       8, [](const uint8_t* d, size_t) {
        return d[0] == '!' && d[1] == '<' && d[2] == 'a' &&
               d[3] == 'r' && d[4] == 'c' && d[5] == 'h' &&
               d[6] == '>' && d[7] == '\n'; }},
    { ArchiveType::Vhd,      8, [](const uint8_t* d, size_t) {
        return d[0] == 'c' && d[1] == 'o' && d[2] == 'n' &&
               d[3] == 'e' && d[4] == 'c' && d[5] == 't' &&
               d[6] == 'i' && d[7] == 'x'; }},
    { ArchiveType::Vmdk,     4, [](const uint8_t* d, size_t) {
        return d[0] == 'K' && d[1] == 'D' && d[2] == 'M' && d[3] == 'V'; }},
    { ArchiveType::Qcow,     4, [](const uint8_t* d, size_t) {
        return d[0] == 'Q' && d[1] == 'F' && d[2] == 'I' && d[3] == 0xFB; }},
    { ArchiveType::Nrg,      5, [](const uint8_t* d, size_t) {
        return (d[0] == 'N' && d[1] == 'E' && d[2] == 'R' && d[3] == '5') ||
               (d[0] == 'N' && d[1] == 'e' && d[2] == 'r' && d[3] == 'o' && d[4] == '5'); }},
    { ArchiveType::Adf,      3, [](const uint8_t* d, size_t) {
        return d[0] == 'D' && d[1] == 'O' && d[2] == 'S'; }},
    { ArchiveType::Wad,      4, [](const uint8_t* d, size_t) {
        return (d[0] == 'I' && d[1] == 'W' && d[2] == 'A' && d[3] == 'D') ||
               (d[0] == 'P' && d[1] == 'W' && d[2] == 'A' && d[3] == 'D') ||
               (d[0] == 'W' && d[1] == 'A' && d[2] == 'D' && d[3] == '2') ||
               (d[0] == 'W' && d[1] == 'A' && d[2] == 'D' && d[3] == '3'); }},
    { ArchiveType::Pak,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'P' && d[1] == 'A' && d[2] == 'C' && d[3] == 'K'; }},
    { ArchiveType::Grp,     12, [](const uint8_t* d, size_t) {
        return d[0] == 'K' && d[1] == 'e' && d[2] == 'n' && d[3] == 'S' &&
               d[4] == 'i' && d[5] == 'l' && d[6] == 'v' && d[7] == 'e' &&
               d[8] == 'r' && d[9] == 'm' && d[10] == 'a' && d[11] == 'n'; }},
    { ArchiveType::Hog,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'H' && d[1] == 'O' && d[2] == 'G' && d[3] == 0xF0; }},
    { ArchiveType::Vpk,      4, [](const uint8_t* d, size_t) {
        return d[0] == 0x34 && d[1] == 0x12 && d[2] == 0xAA && d[3] == 0x55; }},
    { ArchiveType::Mpq,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'M' && d[1] == 'P' && d[2] == 'Q' && d[3] == 0x1A; }},
    { ArchiveType::Xz,       6, [](const uint8_t* d, size_t) {
        return d[0] == 0xFD && d[1] == 0x37 && d[2] == 0x7A &&
               d[3] == 0x58 && d[4] == 0x5A && d[5] == 0x00; }},
    { ArchiveType::Zstd,     4, [](const uint8_t* d, size_t) {
        return d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD; }},
    { ArchiveType::Lz4,      4, [](const uint8_t* d, size_t) {
        return d[0] == 0x04 && d[1] == 0x22 && d[2] == 0x4D && d[3] == 0x18; }},
    { ArchiveType::Bzip2,    3, [](const uint8_t* d, size_t) {
        return d[0] == 'B' && d[1] == 'Z' && d[2] == 'h'; }},
    { ArchiveType::Gob,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'G' && d[1] == 'O' && d[2] == 'B' && d[3] == ' '; }},
    { ArchiveType::Rff,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'R' && d[1] == 'F' && d[2] == 'F' && d[3] == 0x1A; }},
    { ArchiveType::Big,      4, [](const uint8_t* d, size_t) {
        return (d[0] == 'B' && d[1] == 'I' && d[2] == 'G' && d[3] == 'F') ||
               (d[0] == 'B' && d[1] == 'I' && d[2] == 'G' && d[3] == '4'); }},
    { ArchiveType::Pod,      4, [](const uint8_t* d, size_t) {
        return d[0] == 'P' && d[1] == 'O' && d[2] == 'D' &&
               (d[3] == '1' || d[3] == '2' || d[3] == '3' || d[3] == '5'); }},
    { ArchiveType::Chd,      8, [](const uint8_t* d, size_t) {
        return d[0] == 'M' && d[1] == 'C' && d[2] == 'o' && d[3] == 'm' &&
               d[4] == 'p' && d[5] == 'r' && d[6] == 'H' && d[7] == 'D'; }},
    { ArchiveType::Dsk,      8, [](const uint8_t* d, size_t) {
        return (d[0] == 'M' && d[1] == 'V' && d[2] == ' ' && d[3] == '-' &&
                d[4] == ' ' && d[5] == 'C' && d[6] == 'P' && d[7] == 'C') ||
               (d[0] == 'E' && d[1] == 'X' && d[2] == 'T' && d[3] == 'E' &&
                d[4] == 'N' && d[5] == 'D' && d[6] == 'E' && d[7] == 'D'); }},
    { ArchiveType::Dsk,      4, [](const uint8_t* d, size_t) {
        return d[0] == '2' && d[1] == 'I' && d[2] == 'M' && d[3] == 'G'; }},
    { ArchiveType::Cdi,     12, [](const uint8_t* d, size_t) {
        return d[0] == 0x00 && d[1] == 0xFF && d[2] == 0xFF && d[3] == 0xFF &&
               d[4] == 0xFF && d[5] == 0xFF && d[6] == 0xFF && d[7] == 0xFF &&
               d[8] == 0xFF && d[9] == 0xFF && d[10] == 0xFF && d[11] == 0x00; }},
};

ArchiveType FileSignature::Detect(std::string_view path)
{
    FILE* f = std::fopen(std::string(path).c_str(), "rb");
    if (!f) return ArchiveType::Unknown;

    uint8_t header[32];
    size_t n = std::fread(header, 1, sizeof(header), f);
    std::fclose(f);

    for (const auto& sig : kSignatures)
    {
        if (n >= sig.minBytes && sig.match(header, n))
            return sig.type;
    }
    return ArchiveType::Unknown;
}
