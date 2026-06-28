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

class ArchiveEngine;

// Mounts a subset of an open archive as a read-only FUSE filesystem.
// Files are extracted on demand when the drop target reads them.
// unmount() waits for all open file handles to be closed before tearing
// down the mount, so the drop target always gets complete file data.
class FuseArchiveMount
{
public:
    struct Entry {
        std::string archivePath; // key into engine
        std::string mountPath;   // relative path inside the mount (may contain '/')
        uint64_t    size = 0;
    };

    explicit FuseArchiveMount(ArchiveEngine* engine);
    ~FuseArchiveMount();

    void addEntry(const std::string& archivePath, const std::string& mountPath, uint64_t size);

    // Starts the FUSE thread. Returns false if mount fails.
    bool start();

    // Waits until all open file handles are closed (max 60 s), then stops
    // the FUSE loop and blocks until the thread exits.
    void unmount();

    const std::string& mountPoint() const { return m_mountPoint; }

    // Public so the static FUSE op trampolines in the .cpp can access them.
    ArchiveEngine*           m_engine;
    std::string              m_mountPoint;
    std::vector<Entry>       m_entries;
    std::mutex               m_engineMutex;
    void*                    m_session = nullptr; // struct fuse*, opaque here
    std::thread              m_thread;

    std::unordered_map<std::string, const Entry*>             m_byMountPath;
    std::unordered_map<std::string, std::vector<std::string>> m_dirs;

    // Reference count of currently open file handles; guarded by m_idleMutex.
    std::atomic<int>        m_openFiles{0};
    std::mutex              m_idleMutex;
    std::condition_variable m_idleCv;

private:
    void buildTree();
};

#endif // ZIPFX_HAVE_FUSE
#endif // ZIPFX_FUSE_ARCHIVE_MOUNT_H
