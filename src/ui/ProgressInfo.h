#ifndef ZIPFX_PROGRESS_INFO_H
#define ZIPFX_PROGRESS_INFO_H

#include <QElapsedTimer>
#include <QString>

#include <cstdint>

struct ProgressInfo
{
    QElapsedTimer timer;
    uint64_t totalBytes = 0;
    uint64_t bytesProcessed = 0;
    double smoothedRate = 0.0; // bytes/sec (EWMA)
    bool hasRate = false;
    int64_t lastUpdateMs = 0;  // ms since timer start at last UI update
    int64_t lastRateMs = 0;
    uint64_t lastRateBytes = 0;

    void start(uint64_t total)
    {
        totalBytes = total;
        bytesProcessed = 0;
        smoothedRate = 0.0;
        hasRate = false;
        lastUpdateMs = 0;
        lastRateMs = 0;
        lastRateBytes = 0;
        timer.start();
    }

    bool shouldUpdate()
    {
        auto elapsed = timer.elapsed();
        if (elapsed - lastUpdateMs >= 1000)
        {
            lastUpdateMs = elapsed;
            return true;
        }
        return false;
    }

    void addBytes(uint64_t bytes)
    {
        bytesProcessed += bytes;
    }

    int percent() const
    {
        if (totalBytes == 0) return 0;
        if (bytesProcessed >= totalBytes) return 100;
        return static_cast<int>(bytesProcessed * 100 / totalBytes);
    }

    QString etaString() const
    {
        auto elapsed = timer.elapsed();
        if (elapsed < 1000) return {};
        if (bytesProcessed == 0) return {};

        if (smoothedRate < 1.0) return {};

        uint64_t remaining = (totalBytes > bytesProcessed) ? (totalBytes - bytesProcessed) : 0;
        if (remaining == 0) return {};

        int64_t etaSec = static_cast<int64_t>(remaining / smoothedRate);
        if (etaSec < 0) etaSec = 0;

        QString speedStr;
        if (smoothedRate >= 1e9)
            speedStr = QString::number(smoothedRate / 1e9, 'f', 1) + " GB/s";
        else if (smoothedRate >= 1e6)
            speedStr = QString::number(smoothedRate / 1e6, 'f', 1) + " MB/s";
        else if (smoothedRate >= 1e3)
            speedStr = QString::number(smoothedRate / 1e3, 'f', 0) + " KB/s";
        else
            speedStr = QString::number(smoothedRate, 'f', 0) + " B/s";

        QString etaTimeStr;
        if (etaSec >= 86400)
            etaTimeStr = QString("%1d %2h").arg(etaSec / 86400).arg((etaSec % 86400) / 3600);
        else if (etaSec >= 3600)
            etaTimeStr = QString("%1h %2m").arg(etaSec / 3600).arg((etaSec % 3600) / 60);
        else
            etaTimeStr = QString("%1m %2s").arg(etaSec / 60).arg(etaSec % 60, 2, 10, QChar('0'));

        return QString("%1 \xe2\x80\xa2 ETA %2").arg(speedStr, etaTimeStr);
    }

    void updateRate()
    {
        auto elapsed = timer.elapsed();
        if (elapsed < 1000) return;
        if (bytesProcessed == 0) return;

        int64_t dtMs = elapsed - lastRateMs;
        if (dtMs <= 0) return;
        uint64_t dtBytes = bytesProcessed - lastRateBytes;
        lastRateMs = elapsed;
        lastRateBytes = bytesProcessed;

        double rate = dtBytes / (dtMs / 1000.0);

        if (!hasRate)
        {
            smoothedRate = rate;
            hasRate = true;
        }
        else
        {
            smoothedRate = smoothedRate * 0.8 + rate * 0.2;
        }
    }
};

#endif
