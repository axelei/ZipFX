#pragma once

#include <QString>
#include <functional>
#include <cstdint>

// ── XOR-striped recovery sidecar (.zipfx.rec) ─────────────────────────────
//
// Sidecar file format (little-endian):
//   "ZFXREC01"          8 bytes  magic
//   originalSize        uint64   total bytes in the protected file
//   blockSize           uint32   bytes per data / recovery block
//   dataBlockCount      uint32   ceil(originalSize / blockSize)
//   recoveryBlockCount  uint32   number of recovery (parity) blocks
//   originalCRC32       uint32   crc32 of the whole file
//   blockCRC32[]        uint32   one per data block (for locating corruption)
//   recoveryBlock[]     blockSize bytes each
//
// Recovery algorithm (striped XOR, RAID-5 style):
//   recovery[i] = XOR of all data blocks where (blockIndex % recoveryCount == i)
//
// Guarantees: any single data block per stripe can be reconstructed.
// With R recovery blocks you can reconstruct up to R blocks total, provided
// they are spread across different stripes (one corrupted block per stripe).

class RecoveryRecord
{
public:
    struct Options {
        uint32_t blockSize       = 65536; // 64 KB
        int      recoveryPercent = 5;     // % of data blocks → recovery blocks
    };

    struct VerifyResult {
        bool    ok             = true;
        int     totalBlocks    = 0;
        int     badBlocks      = 0;
        int     repairedBlocks = 0;
        QString errorMessage;
    };

    // Returns path of the sidecar for a given archive path.
    static QString sidecarPath(const QString& archivePath);

    // Create a .zipfx.rec sidecar next to the archive.
    // progress(0..100) is called periodically; return empty string on success.
    static QString create(const QString& archivePath,
                          const Options& opts,
                          std::function<void(int)> progress = nullptr);

    static QString create(const QString& archivePath)
    {
        Options defaults;
        return create(archivePath, defaults, nullptr);
    }

    // Verify the archive against its sidecar.  If repair=true, corrupted blocks
    // are rewritten in-place (requires write access to the archive).
    static VerifyResult verify(const QString& archivePath, bool repair,
                               std::function<void(int)> progress = nullptr);

    // Returns true if a sidecar file exists for the given archive.
    static bool hasSidecar(const QString& archivePath);
};
