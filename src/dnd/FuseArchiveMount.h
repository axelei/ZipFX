#ifndef ZIPFX_FUSE_ARCHIVE_MOUNT_H
#define ZIPFX_FUSE_ARCHIVE_MOUNT_H

#ifdef ZIPFX_HAVE_FUSE

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <cstdint>

class ArchiveEngine;

// Mounts a subset of an open archive as a read-only FUSE filesystem.
// All files are extracted into memory inside start() on the caller's thread
// before the FUSE session begins. FUSE ops never touch the engine, so there
// is no race with the archive engine being used on the main thread.
//
// Security: mkdtemp creates the mount point with 0700 (owner-only).
// FUSE without allow_other only permits the mounting user, so no other
// user on the system can access the files.
//
// Lifecycle: call start(), pass URLs to QDrag, then detach unmount()
// onto a background std::thread — it blocks until the drop target
// finishes reading (max 65 s) and cleans up without touching the UI thread.
class FuseArchiveMount
{
public:
    struct Entry {
        std::string          archivePath; // key used during extraction
        std::string          mountPath;   // relative path inside the mount
        uint64_t             size = 0;
        uint32_t             permissions = 0644; // Unix mode bits
        std::vector<uint8_t> data;        // extracted content, filled by start()
    };

    explicit FuseArchiveMount(ArchiveEngine* engine);
    ~FuseArchiveMount();

    void addEntry(const std::string& archivePath,
                  const std::string& mountPath,
                  uint64_t size,
                  uint32_t permissions = 0644);

    // Extracts all entries into memory, mounts the FUSE filesystem, and starts
    // the background thread. Returns false if extraction or mount fails.
    bool start();

    // Tears down the mount. If immediate is true, skip the two-phase wait
    // (used when QDrag::exec returned IgnoreAction — no copy is in progress).
    // Otherwise blocks until the drop target has opened and closed all file
    // handles (max 65 s). Call from a background thread so the main thread
    // stays responsive.
    void unmount(bool immediate = false);

    const std::string& mountPoint() const { return m_mountPoint; }

    // Abort a running unmount wait. Called from MainWindow when a new drag
    // starts while the previous mount is still in its grace-period wait.
    void abandon();

    // Public so the static FUSE op trampolines in the .cpp can access them.
    std::string              m_mountPoint;
    std::vector<Entry>       m_entries;
    void*                    m_session = nullptr; // struct fuse*, opaque here
    std::thread              m_thread;

    std::unordered_map<std::string, const Entry*>             m_byMountPath;
    std::unordered_map<std::string, std::vector<std::string>> m_dirs;

    // Open-file tracking for the two-phase unmount wait.
    std::atomic<int>         m_openFiles{0};
    std::atomic<bool>        m_everOpened{false};
    std::atomic<uint64_t>    m_totalReadBytes{0}; // total bytes read via FUSE
    std::mutex               m_cvMutex;
    std::condition_variable  m_cv;

    // Returns true if any FuseArchiveMount has an active FUSE thread.
    // Used by MainWindow to fall back to eager extraction when a previous
    // drag's unmount is still in progress, preventing multiple concurrent
    // FUSE threads that can corrupt libfuse internal state.
    static bool hasActiveMount();

private:
    ArchiveEngine* m_engine; // only used during start(), not after
    bool           m_active = false; // true while counted in s_activeMounts
    std::atomic<bool> m_abandoned{false};
    std::atomic<bool> m_unmountRequested{false};
    void buildTree();
};

#endif // ZIPFX_HAVE_FUSE
#endif // ZIPFX_FUSE_ARCHIVE_MOUNT_H
