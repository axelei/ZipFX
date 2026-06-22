#ifndef ZIPFX_FILE_SIGNATURE_H
#define ZIPFX_FILE_SIGNATURE_H

#include <cstdint>
#include <string_view>

enum class ArchiveType
{
    Unknown,
    Zip,
    SevenZip,
    Rar,
    Rar5,
    Gzip,
    Cab,
    Iso,
    Lha,
    Xar,
    Cpio
};

struct FileSignature
{
    static ArchiveType Detect(std::string_view path);
};

#endif
