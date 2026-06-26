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
    Cpio,
    Ar,
    Vhd,
    Vmdk,
    Qcow,
    Nrg,
    Adf,
    Wad,
    Pak,
    Grp,
    Hog,
    Vpk,
    Mpq,
    Warc,
    Mtree,
    Bzip2,
    Xz,
    Zstd,
    Lz4,
    Gob,
    Rff,
    Big,
    Pod,
    Chd,
    Cdi,
    Gdi,
    Dsk
};

struct FileSignature
{
    static ArchiveType Detect(std::string_view path);
};

#endif
