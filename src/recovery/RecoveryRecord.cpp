#include "RecoveryRecord.h"

#include <QFile>
#include <QFileInfo>
#include <QDataStream>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <vector>

// ── Sidecar magic ──────────────────────────────────────────────────────────
static constexpr char kMagic[8] = {'Z','F','X','R','E','C','0','1'};

// ── CRC32 of a byte range ──────────────────────────────────────────────────
static uint32_t crc32OfRange(const std::vector<uint8_t>& buf,
                              qint64 offset, qint64 len)
{
    uLong crc = crc32(0, nullptr, 0);
    crc = crc32(crc, buf.data() + offset, static_cast<uInt>(len));
    return static_cast<uint32_t>(crc);
}

// ── QString path of the sidecar ────────────────────────────────────────────
QString RecoveryRecord::sidecarPath(const QString& archivePath)
{
    return archivePath + ".rec";
}

bool RecoveryRecord::hasSidecar(const QString& archivePath)
{
    return QFileInfo::exists(sidecarPath(archivePath));
}

// ── Create sidecar ─────────────────────────────────────────────────────────
QString RecoveryRecord::create(const QString& archivePath,
                               const Options& opts,
                               std::function<void(int)> progress)
{
    QFile src(archivePath);
    if (!src.open(QIODevice::ReadOnly))
        return QString("Cannot open: %1").arg(archivePath);

    const qint64    fileSize  = src.size();
    const uint32_t  blockSize = opts.blockSize;
    const uint32_t  dataCount = static_cast<uint32_t>(
        (fileSize + blockSize - 1) / blockSize);

    if (dataCount == 0)
        return QString("Archive is empty.");

    // At least 1 recovery block; proportional to dataCount
    const uint32_t recCount = std::max(1u,
        static_cast<uint32_t>(dataCount * opts.recoveryPercent / 100));

    // ── Read all data blocks ──────────────────────────────────────────────
    std::vector<uint32_t> blockCRC(dataCount);
    std::vector<std::vector<uint8_t>> recBlocks(recCount,
                                                  std::vector<uint8_t>(blockSize, 0));

    // Compute whole-file CRC32
    uLong fileCRC = crc32(0, nullptr, 0);

    for (uint32_t i = 0; i < dataCount; ++i)
    {
        if (progress) progress(static_cast<int>(i * 80 / dataCount));

        std::vector<uint8_t> blk(blockSize, 0);
        qint64 bytesRead = src.read(reinterpret_cast<char*>(blk.data()), blockSize);
        if (bytesRead < 0) return QString("Read error at block %1").arg(i);

        fileCRC = crc32(fileCRC, blk.data(), static_cast<uInt>(bytesRead));
        blockCRC[i] = crc32OfRange(blk, 0, bytesRead);

        // XOR into the appropriate recovery block (stripe index = i % recCount)
        uint32_t stripe = i % recCount;
        for (uint32_t b = 0; b < blockSize; ++b)
            recBlocks[stripe][b] ^= blk[b];
    }
    src.close();

    if (progress) progress(85);

    // ── Write sidecar ─────────────────────────────────────────────────────
    QFile out(sidecarPath(archivePath));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString("Cannot write sidecar: %1").arg(sidecarPath(archivePath));

    QDataStream ds(&out);
    ds.setByteOrder(QDataStream::LittleEndian);

    out.write(kMagic, 8);
    ds << static_cast<quint64>(fileSize);
    ds << static_cast<quint32>(blockSize);
    ds << static_cast<quint32>(dataCount);
    ds << static_cast<quint32>(recCount);
    ds << static_cast<quint32>(fileCRC);

    for (uint32_t i = 0; i < dataCount; ++i)
        ds << static_cast<quint32>(blockCRC[i]);

    for (uint32_t r = 0; r < recCount; ++r)
        out.write(reinterpret_cast<const char*>(recBlocks[r].data()), blockSize);

    out.close();

    if (progress) progress(100);
    return {};
}

// ── Verify (and optionally repair) ────────────────────────────────────────
RecoveryRecord::VerifyResult RecoveryRecord::verify(const QString& archivePath,
                                                     bool repair,
                                                     std::function<void(int)> progress)
{
    VerifyResult result;

    // ── Open and parse the sidecar ────────────────────────────────────────
    QFile sidecar(sidecarPath(archivePath));
    if (!sidecar.open(QIODevice::ReadOnly))
    {
        result.ok = false;
        result.errorMessage = "Sidecar (.rec) file not found.";
        return result;
    }

    char magic[8] = {};
    sidecar.read(magic, 8);
    if (memcmp(magic, kMagic, 8) != 0)
    {
        result.ok = false;
        result.errorMessage = "Sidecar file is not a valid ZipFX recovery record.";
        return result;
    }

    QDataStream ds(&sidecar);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint64 storedSize;
    quint32 blockSize, dataCount, recCount, storedFileCRC;
    ds >> storedSize >> blockSize >> dataCount >> recCount >> storedFileCRC;

    std::vector<uint32_t> blockCRC(dataCount);
    for (uint32_t i = 0; i < dataCount; ++i)
    {
        quint32 c; ds >> c;
        blockCRC[i] = c;
    }

    // Read recovery blocks
    std::vector<std::vector<uint8_t>> recBlocks(recCount,
                                                  std::vector<uint8_t>(blockSize, 0));
    for (uint32_t r = 0; r < recCount; ++r)
        sidecar.read(reinterpret_cast<char*>(recBlocks[r].data()), blockSize);

    sidecar.close();

    result.totalBlocks = static_cast<int>(dataCount);

    // ── Read the archive and check block CRCs ─────────────────────────────
    QFile src(archivePath);
    if (!src.open(repair ? QIODevice::ReadWrite : QIODevice::ReadOnly))
    {
        result.ok = false;
        result.errorMessage = repair
            ? "Cannot open archive for writing (check permissions)."
            : "Cannot open archive for reading.";
        return result;
    }

    // Map: stripe → list of (blockIndex, block_data) for corrupt blocks in stripe
    struct BadBlock { uint32_t idx; std::vector<uint8_t> data; };
    std::vector<std::vector<BadBlock>> badByStripe(recCount);
    bool anyBad = false;

    for (uint32_t i = 0; i < dataCount; ++i)
    {
        if (progress) progress(static_cast<int>(i * 70 / dataCount));

        std::vector<uint8_t> blk(blockSize, 0);
        qint64 bytesRead = src.read(reinterpret_cast<char*>(blk.data()), blockSize);
        if (bytesRead < 0) { result.ok = false; result.errorMessage = "Read error."; return result; }

        uint32_t actualCRC = crc32OfRange(blk, 0, bytesRead);
        if (actualCRC != blockCRC[i])
        {
            ++result.badBlocks;
            anyBad = true;
            badByStripe[i % recCount].push_back({i, blk});
        }
    }

    if (progress) progress(75);

    result.ok = !anyBad;

    if (!anyBad || !repair)
    {
        src.close();
        if (!anyBad) result.errorMessage.clear();
        return result;
    }

    // ── Attempt repair ────────────────────────────────────────────────────
    // Strategy: for each stripe, if exactly 1 block is corrupted,
    // recompute it as XOR of all good blocks in the stripe ^ recovery block.
    for (uint32_t stripe = 0; stripe < recCount; ++stripe)
    {
        if (badByStripe[stripe].size() != 1)
            continue; // can't fix; 0 bad = fine, >1 = unrecoverable

        uint32_t badIdx = badByStripe[stripe][0].idx;

        // XOR recovery block with all GOOD blocks in the stripe
        std::vector<uint8_t> repaired = recBlocks[stripe]; // start with parity

        // Re-read all good blocks in this stripe
        for (uint32_t i = stripe; i < dataCount; i += recCount)
        {
            if (i == badIdx) continue;

            src.seek(static_cast<qint64>(i) * blockSize);
            std::vector<uint8_t> blk(blockSize, 0);
            qint64 n = src.read(reinterpret_cast<char*>(blk.data()), blockSize);
            if (n < 0) continue;
            for (uint32_t b = 0; b < blockSize; ++b)
                repaired[b] ^= blk[b];
        }

        // Verify the repaired block
        uint32_t repairedCRC = crc32OfRange(repaired, 0, blockSize);
        if (repairedCRC == blockCRC[badIdx])
        {
            // Write repaired block back
            src.seek(static_cast<qint64>(badIdx) * blockSize);
            src.write(reinterpret_cast<const char*>(repaired.data()), blockSize);
            ++result.repairedBlocks;
        }
    }

    src.close();

    if (progress) progress(100);

    result.ok = (result.repairedBlocks == result.badBlocks);
    if (!result.ok)
        result.errorMessage = QString("%1 block(s) could not be repaired "
            "(multiple corrupted blocks in the same stripe exceed recovery capacity).")
            .arg(result.badBlocks - result.repairedBlocks);

    return result;
}
