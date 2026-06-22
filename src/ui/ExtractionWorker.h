#ifndef ZIPFX_EXTRACTION_WORKER_H
#define ZIPFX_EXTRACTION_WORKER_H

#include <QString>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

class ArchiveEngine;
struct ArchiveEntry;

// Background thread that extracts files. Progress is polled via atomics.
class ExtractionWorker
{
public:
    ExtractionWorker(ArchiveEngine* engine,
                     std::vector<ArchiveEntry> entries,
                     const QString& destPath, bool preserveStructure);
    ~ExtractionWorker();

    void Start();
    void Cancel();
    void Pause();
    void Resume();
    bool IsPaused() const { return m_paused; }
    bool IsRunning() const { return m_thread.joinable(); }

    // Polled from main thread — atomic
    std::atomic<int>  m_progressCount{0};
    std::atomic<int>  m_progressTotal{0};
    std::atomic<bool> m_finished{false};

private:
    void Run();

    ArchiveEngine*            m_engine;
    std::vector<ArchiveEntry> m_entries;
    QString                   m_destPath;
    bool                      m_preserveStructure;

    std::thread               m_thread;
    std::atomic<bool>         m_cancelled{false};
    std::atomic<bool>         m_paused{false};
    std::mutex                m_pauseMtx;
    std::condition_variable   m_pauseCv;
};

#endif
