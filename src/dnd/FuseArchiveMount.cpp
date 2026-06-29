#include "FuseArchiveMount.h"

#ifdef ZIPFX_HAVE_FUSE

#define FUSE_USE_VERSION 35
#include <fuse.h>
#include <fuse_lowlevel.h>

#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <chrono>

#include "engine/ArchiveEngine.h"
#include "engine/Logging.h"

#include <QCoreApplication>

// Counts mounts that have a live FUSE thread (incremented in start() after
// a successful mount, decremented in unmount() after the FUSE thread is
// joined). hasActiveMount() lets MainWindow avoid starting a second FUSE
// mount while the first is still waiting for the file manager to finish
// reading — multiple concurrent fuse_loop() calls on the same libfuse
// installation can corrupt internal libfuse state and crash.
static std::atomic<int> s_activeMounts{0};

// ── static FUSE op trampolines ────────────────────────────────────────
//
// Each mount passes 'this' as fuse_new()'s private_data so that concurrent
// mounts (two rapid drags) are fully independent — there is no global
// singleton that the second start() could clobber under the first's thread.

// Strip leading '/' from FUSE path
static std::string relpath(const char* cpath)
{
    return (cpath && cpath[0] == '/') ? cpath + 1 : (cpath ? cpath : "");
}

static inline FuseArchiveMount* self()
{
    return static_cast<FuseArchiveMount*>(fuse_get_context()->private_data);
}

static int op_getattr(const char* cpath, struct stat* st, struct fuse_file_info*)
{
    memset(st, 0, sizeof(*st));
    FuseArchiveMount* m = self();
    std::string path = relpath(cpath);

    if (path.empty())
    {
        st->st_mode  = S_IFDIR | 0555;
        st->st_nlink = 2;
        return 0;
    }

    auto it = m->m_byMountPath.find(path);
    if (it != m->m_byMountPath.end())
    {
        st->st_mode  = S_IFREG | (it->second->permissions & 0777);
        st->st_nlink = 1;
        st->st_size  = static_cast<off_t>(it->second->data.size());
        return 0;
    }

    if (m->m_dirs.count(path))
    {
        st->st_mode  = S_IFDIR | 0555;
        st->st_nlink = 2;
        return 0;
    }

    return -ENOENT;
}

static int op_readdir(const char* cpath, void* buf, fuse_fill_dir_t filler,
                      off_t, struct fuse_file_info*, enum fuse_readdir_flags)
{
    FuseArchiveMount* m = self();
    std::string path = relpath(cpath);

    auto it = m->m_dirs.find(path);
    if (it == m->m_dirs.end()) return -ENOENT;

    filler(buf, ".",  nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);
    for (const auto& child : it->second)
        filler(buf, child.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
    return 0;
}

static int op_open(const char* cpath, struct fuse_file_info* fi)
{
    FuseArchiveMount* m = self();
    auto it = m->m_byMountPath.find(relpath(cpath));
    if (it == m->m_byMountPath.end()) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;

    // Point fi->fh at the pre-extracted Entry. All data was loaded in
    // start() on the main thread, so no engine access is needed here and
    // there is no race with the archive engine on the main thread.
    fi->fh = reinterpret_cast<uint64_t>(it->second);
    LOG_DBG("FuseArchiveMount: op_open %s (%zu bytes)",
            cpath, it->second->data.size());
    m->m_everOpened.store(true);
    ++m->m_openFiles;
    m->m_cv.notify_all(); // wake phase-1 of unmount()
    return 0;
}

static int op_release(const char* /*path*/, struct fuse_file_info* /*fi*/)
{
    FuseArchiveMount* m = self();
    if (--m->m_openFiles == 0)
        m->m_cv.notify_all(); // wake phase-2 of unmount()
    return 0;
}

static int op_read(const char* /*path*/, char* buf, size_t size, off_t offset,
                   struct fuse_file_info* fi)
{
    const auto* e = reinterpret_cast<const FuseArchiveMount::Entry*>(fi->fh);
    if (!e) return -EIO;
    if (static_cast<size_t>(offset) >= e->data.size()) return 0;
    size_t n = std::min(size, e->data.size() - static_cast<size_t>(offset));
    memcpy(buf, e->data.data() + offset, n);
    self()->m_totalReadBytes += n;
    return static_cast<int>(n);
}

static const struct fuse_operations s_ops = []() {
    fuse_operations ops{};
    ops.getattr = op_getattr;
    ops.readdir = op_readdir;
    ops.open    = op_open;
    ops.read    = op_read;
    ops.release = op_release;
    return ops;
}();

// ── FuseArchiveMount ──────────────────────────────────────────────

FuseArchiveMount::FuseArchiveMount(ArchiveEngine* engine)
    : m_engine(engine)
{
}

FuseArchiveMount::~FuseArchiveMount()
{
    unmount();
}

void FuseArchiveMount::addEntry(const std::string& archivePath,
                                const std::string& mountPath,
                                uint64_t size,
                                uint32_t permissions)
{
    m_entries.push_back({archivePath, mountPath, size, permissions, {}});
}

void FuseArchiveMount::buildTree()
{
    m_byMountPath.clear();
    m_dirs.clear();
    m_dirs[""] = {};

    for (const Entry& e : m_entries)
    {
        m_byMountPath[e.mountPath] = &e;

        std::string p = e.mountPath;
        while (true)
        {
            auto slash  = p.rfind('/');
            std::string name   = (slash == std::string::npos) ? p : p.substr(slash + 1);
            std::string parent = (slash == std::string::npos) ? "" : p.substr(0, slash);

            auto& children = m_dirs[parent];
            if (std::find(children.begin(), children.end(), name) == children.end())
                children.push_back(name);

            if (slash == std::string::npos) break;
            p = parent;
        }
    }
}

bool FuseArchiveMount::start()
{
    // Mark active BEFORE the extraction loop — it calls processEvents() and
    // the re-entrant onBeginDrag() must see hasActiveMount() == true.
    m_active = true;
    ++s_activeMounts;

    // Extract all files now, on the caller's thread, before the FUSE session
    // starts. FUSE ops (op_open/op_read) will read from these buffers and
    // never touch the engine — eliminating any race with the main thread
    // using the archive engine (e.g. zip_close during Save).
    buildTree();
    for (Entry& e : m_entries)
    {
        e.data = m_engine->ReadFile(e.archivePath);
        if (e.data.empty() && e.size > 0)
            LOG_WARN("FuseArchiveMount: failed to extract '%s'", e.archivePath.c_str());
        QCoreApplication::processEvents();
    }
    m_engine = nullptr; // not used after this point

    char tmpl[] = "/tmp/ZipFX_fuse_XXXXXX";
    if (!mkdtemp(tmpl))
    {
        LOG_ERR("FuseArchiveMount: mkdtemp failed: %s", strerror(errno));
        m_active = false; --s_activeMounts;
        return false;
    }
    m_mountPoint = tmpl;

    const char* argv[] = { "ZipFX", nullptr };
    struct fuse_args args = FUSE_ARGS_INIT(1, const_cast<char**>(argv));

    // Pass 'this' as private_data so each mount's FUSE ops resolve to their
    // own FuseArchiveMount via fuse_get_context()->private_data (see self()).
    struct fuse* f = fuse_new(&args, &s_ops, sizeof(s_ops), this);
    if (!f)
    {
        LOG_ERR("FuseArchiveMount: fuse_new failed: %s", strerror(errno));
        rmdir(m_mountPoint.c_str());
        m_mountPoint.clear();
        m_active = false; --s_activeMounts;
        return false;
    }

    // Block SIGCHLD around fuse_mount to prevent Qt's SIGCHLD handler from
    // reaping fusermount3 before libfuse's internal waitpid can, which causes
    // the mount fd handshake to fail with EPERM.
    sigset_t blockSet, oldSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &blockSet, &oldSet);

    int mountRc = fuse_mount(f, m_mountPoint.c_str());
    int savedErrno = errno;

    pthread_sigmask(SIG_SETMASK, &oldSet, nullptr);

    if (mountRc != 0)
    {
        LOG_ERR("FuseArchiveMount: fuse_mount failed on %s: %s",
                m_mountPoint.c_str(), strerror(savedErrno));
        fuse_destroy(f);
        rmdir(m_mountPoint.c_str());
        m_mountPoint.clear();
        m_active = false; --s_activeMounts;
        return false;
    }

    m_session = f;

    m_thread = std::thread([this, f]() {
        struct fuse_session* se = fuse_get_session(f);
        int fuse_fd = fuse_session_fd(se);

        // Custom poll-based loop instead of fuse_loop(f). This allows the
        // unmount thread to simply set m_unmountRequested and rely on the
        // 1-second poll timeout to unblock the loop, instead of requiring
        // fuse_unmount (which closes /dev/fuse) to be called from a
        // different thread — the root cause of the fd-close-vs-read race
        // that crashes in Release builds.
        while (!m_unmountRequested.load())
        {
            struct pollfd pfd = {fuse_fd, POLLIN, 0};
            int ret = poll(&pfd, 1, 1000);  // 1 s timeout

            if (ret < 0)
            {
                if (errno == EINTR) continue;
                break;
            }

            if (ret > 0 && (pfd.revents & POLLIN))
            {
                struct fuse_buf fbuf;
                memset(&fbuf, 0, sizeof(fbuf));
                int res = fuse_session_receive_buf(se, &fbuf);
                if (res == -EINTR) continue;
                if (res <= 0) break;
                fuse_session_process_buf(se, &fbuf);
            }
        }

        // fuse_unmount and fuse_destroy on the SAME thread that ran the
        // event loop — no cross-thread fd close race.
        fuse_unmount(f);
        fuse_destroy(f);
    });

    LOG_DBG("FuseArchiveMount: mounted at %s (active mounts: %d)",
            m_mountPoint.c_str(), s_activeMounts.load());
    return true;
}

void FuseArchiveMount::abandon()
{
    m_abandoned = true;
    m_cv.notify_all();
}

bool FuseArchiveMount::hasActiveMount()
{
    return s_activeMounts.load() > 0;
}

void FuseArchiveMount::unmount(bool immediate)
{
    if (m_session)
    {
        if (!immediate)
        {
            std::unique_lock<std::mutex> lk(m_cvMutex);

            // Phase 1: wait for the drop target to open at least one file,
            // or until abandon() is called by a new drag.
            LOG_DBG("FuseArchiveMount: unmount phase-1 wait (mount=%s)", m_mountPoint.c_str());
            m_cv.wait_for(lk, std::chrono::seconds(60),
                          [this] { return m_everOpened.load() || m_abandoned.load(); });

            // Phase 2: wait for all handles to close, with a grace period for
            // re-opens. We track total bytes read via FUSE to distinguish a
            // normal copy (file fully read) from a metadata probe (only a few
            // bytes read, e.g. for an "Overwrite?" dialog).
            if (!m_abandoned.load() && m_everOpened.load())
            {
                uint64_t totalSize = 0;
                for (const auto& e : m_entries)
                    totalSize += e.size;

                bool abandoned = false;
                auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds(60);
                while (!abandoned && std::chrono::steady_clock::now() < deadline)
                {
                    if (m_abandoned.load()) { abandoned = true; break; }

                    auto remaining = std::chrono::duration_cast<
                        std::chrono::milliseconds>(deadline
                        - std::chrono::steady_clock::now());
                    if (remaining.count() <= 0) break;

                    bool closed = m_cv.wait_for(lk, remaining,
                        [this] { return m_openFiles.load() == 0
                                      || m_abandoned.load(); });
                    if (m_abandoned.load()) { abandoned = true; break; }
                    if (!closed) break;

                    // Grace period for re-open. Use a short grace (500 ms)
                    // when all bytes were already read (normal copy), or a
                    // long grace (60 s) when only a probe occurred (the file
                    // manager is likely showing a dialog).
                    bool allRead = m_totalReadBytes.load() >= totalSize;
                    auto grace = allRead ? std::chrono::milliseconds(500)
                                         : std::chrono::seconds(60);
                    bool reopened = m_cv.wait_for(lk, grace,
                        [this] { return m_openFiles.load() > 0
                                      || m_abandoned.load(); });
                    if (m_abandoned.load()) { abandoned = true; break; }
                    if (!reopened) break;
                }
            }
        }
        else
            LOG_DBG("FuseArchiveMount: immediate unmount — skipping two-phase wait");

        // Signal the FUSE thread to exit its poll loop.
        // It will call fuse_unmount + fuse_destroy on the same thread,
        // avoiding the libfuse3 close-fd-vs-read race that crashes in
        // Release builds.
        m_unmountRequested = true;
    }
    if (m_thread.joinable())
        m_thread.join();
    m_session = nullptr;
    if (m_active)
    {
        --s_activeMounts;
        m_active = false;
        LOG_DBG("FuseArchiveMount: FUSE thread exited (active mounts now: %d)", s_activeMounts.load());
    }
    if (!m_mountPoint.empty())
    {
        rmdir(m_mountPoint.c_str());
        m_mountPoint.clear();
    }
}

#endif // ZIPFX_HAVE_FUSE
