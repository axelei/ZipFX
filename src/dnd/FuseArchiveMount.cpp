#include "FuseArchiveMount.h"

#ifdef ZIPFX_HAVE_FUSE

#define FUSE_USE_VERSION 35
#include <fuse.h>

#include <sys/stat.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <chrono>

#include "engine/ArchiveEngine.h"
#include "engine/Logging.h"

// ── static FUSE op trampolines ────────────────────────────────────

static FuseArchiveMount* s_mount = nullptr;

// Strip leading '/' from FUSE path
static std::string relpath(const char* cpath)
{
    return (cpath && cpath[0] == '/') ? cpath + 1 : (cpath ? cpath : "");
}

static int op_getattr(const char* cpath, struct stat* st, struct fuse_file_info*)
{
    memset(st, 0, sizeof(*st));
    FuseArchiveMount* m = s_mount;
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
        st->st_mode  = S_IFREG | 0444;
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
    FuseArchiveMount* m = s_mount;
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
    FuseArchiveMount* m = s_mount;
    auto it = m->m_byMountPath.find(relpath(cpath));
    if (it == m->m_byMountPath.end()) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;

    // Point fi->fh at the pre-extracted Entry. All data was loaded in
    // start() on the main thread, so no engine access is needed here and
    // there is no race with the archive engine on the main thread.
    fi->fh = reinterpret_cast<uint64_t>(it->second);
    m->m_everOpened.store(true);
    ++m->m_openFiles;
    m->m_cv.notify_all(); // wake phase-1 of unmount()
    return 0;
}

static int op_release(const char* /*path*/, struct fuse_file_info* /*fi*/)
{
    FuseArchiveMount* m = s_mount;
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
                                uint64_t size)
{
    m_entries.push_back({archivePath, mountPath, size, {}});
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
    }
    m_engine = nullptr; // not used after this point

    char tmpl[] = "/tmp/ZipFX_fuse_XXXXXX";
    if (!mkdtemp(tmpl))
    {
        LOG_ERR("FuseArchiveMount: mkdtemp failed: %s", strerror(errno));
        return false;
    }
    m_mountPoint = tmpl;
    s_mount = this;

    const char* argv[] = { "ZipFX", nullptr };
    struct fuse_args args = FUSE_ARGS_INIT(1, const_cast<char**>(argv));

    struct fuse* f = fuse_new(&args, &s_ops, sizeof(s_ops), nullptr);
    if (!f)
    {
        LOG_ERR("FuseArchiveMount: fuse_new failed: %s", strerror(errno));
        rmdir(m_mountPoint.c_str());
        m_mountPoint.clear();
        s_mount = nullptr;
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
        s_mount = nullptr;
        return false;
    }

    m_session = f;

    m_thread = std::thread([f]() {
        fuse_loop(f);
        // fuse_unmount() is called by the unmount thread before join(), which
        // causes read(/dev/fuse) to return ENODEV and fuse_loop to exit. Do
        // not call fuse_unmount here — just free the fuse object.
        fuse_destroy(f);
    });

    LOG_DBG("FuseArchiveMount: mounted at %s", m_mountPoint.c_str());
    return true;
}

void FuseArchiveMount::unmount()
{
    if (m_session)
    {
        std::unique_lock<std::mutex> lk(m_cvMutex);

        // Phase 1: wait up to 5 s for the drop target to open at least one file.
        // Without this, m_openFiles is still 0 and we'd tear down immediately
        // before the file manager has had a chance to start reading.
        m_cv.wait_for(lk, std::chrono::seconds(5),
                      [this] { return m_everOpened.load(); });

        // Phase 2: wait for all handles to close, with a 500 ms grace period
        // between closes to handle file managers that open files sequentially
        // (they close file A before opening file B, creating a momentary zero).
        if (m_everOpened.load())
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (true)
            {
                // Wait for m_openFiles to reach 0 (or deadline).
                bool closed = m_cv.wait_until(lk, deadline,
                    [this] { return m_openFiles.load() == 0; });
                if (!closed) break; // 60 s hard cap

                // Grace period: if the file manager opens another file within
                // 500 ms, loop back and wait for that one to close too.
                bool reopened = m_cv.wait_for(lk, std::chrono::milliseconds(500),
                    [this] { return m_openFiles.load() > 0; });
                if (!reopened) break; // still idle — file manager is done
            }
        }

        // fuse_unmount() triggers the kernel to remove the FUSE filesystem,
        // which causes fuse_loop()'s blocking read(/dev/fuse) to get ENODEV
        // and return. This is cleaner than fuse_exit() + dummy stat, which
        // is unreliable because fuse_loop only checks the exit flag before
        // each request, not mid-read.
        fuse_unmount(static_cast<struct fuse*>(m_session));
        m_session = nullptr;
    }
    if (m_thread.joinable())
        m_thread.join();
    if (!m_mountPoint.empty())
    {
        rmdir(m_mountPoint.c_str());
        m_mountPoint.clear();
    }
    if (s_mount == this)
        s_mount = nullptr;
}

#endif // ZIPFX_HAVE_FUSE
