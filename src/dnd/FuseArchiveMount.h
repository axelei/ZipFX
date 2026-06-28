#ifndef ZIPFX_FUSE_ARCHIVE_MOUNT_H
#define ZIPFX_FUSE_ARCHIVE_MOUNT_H

#ifdef ZIPFX_HAVE_FUSE

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>

class ArchiveEngine;

// Mounts a subset of an open archive as a read-only FUSE filesystem.
// Files are extracted on demand when the drop target reads them.
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

    // Signals the FUSE loop to stop and blocks until the thread exits.
    void unmount();

    const std::string& mountPoint() const { return m_mountPoint; }

    // These are public so the static FUSE op trampolines in the .cpp can access them.
    // This is an internal class — not part of any public API.
    ArchiveEngine*           m_engine;
    std::string              m_mountPoint;
    std::vector<Entry>       m_entries;
    std::mutex               m_engineMutex;
    void*                    m_session = nullptr; // fuse_session*, opaque here
    std::thread              m_thread;

    std::unordered_map<std::string, const Entry*>             m_byMountPath;
    std::unordered_map<std::string, std::vector<std::string>> m_dirs;

private:
    void buildTree();
};

#endif // ZIPFX_HAVE_FUSE
#endif // ZIPFX_FUSE_ARCHIVE_MOUNT_H
