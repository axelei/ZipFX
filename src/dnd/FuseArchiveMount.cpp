#include "FuseArchiveMount.h"

#ifdef ZIPFX_HAVE_FUSE

#define FUSE_USE_VERSION 35
#include <fuse.h>

#include <sys/stat.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <algorithm>

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
        st->st_size  = static_cast<off_t>(it->second->size);
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
    if (!m->m_byMountPath.count(relpath(cpath))) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    return 0;
}

static int op_read(const char* cpath, char* buf, size_t size, off_t offset,
                   struct fuse_file_info*)
{
    FuseArchiveMount* m = s_mount;
    auto it = m->m_byMountPath.find(relpath(cpath));
    if (it == m->m_byMountPath.end()) return -ENOENT;

    const FuseArchiveMount::Entry* e = it->second;
    if (static_cast<uint64_t>(offset) >= e->size) return 0;

    std::lock_guard<std::mutex> lock(m->m_engineMutex);

    int filled = 0;
    uint64_t pos = 0;
    const uint64_t wantStart = static_cast<uint64_t>(offset);
    const uint64_t wantEnd   = wantStart + size;
    bool done = false;

    m->m_engine->ReadFileStreamed(e->archivePath, [&](const uint8_t* data, size_t len) -> bool {
        const uint64_t chunkEnd = pos + len;
        if (chunkEnd > wantStart && pos < wantEnd)
        {
            size_t copyFrom = (pos < wantStart) ? static_cast<size_t>(wantStart - pos) : 0;
            size_t copyEnd  = static_cast<size_t>(std::min(chunkEnd, wantEnd) - pos);
            size_t n        = copyEnd - copyFrom;
            memcpy(buf + filled, data + copyFrom, n);
            filled += static_cast<int>(n);
        }
        pos += len;
        if (pos >= wantEnd) done = true;
        return !done;
    });

    return filled;
}

static const struct fuse_operations s_ops = []() {
    fuse_operations ops{};
    ops.getattr = op_getattr;
    ops.readdir = op_readdir;
    ops.open    = op_open;
    ops.read    = op_read;
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
    m_entries.push_back({archivePath, mountPath, size});
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
    char tmpl[] = "/tmp/ZipFX_fuse_XXXXXX";
    if (!mkdtemp(tmpl))
    {
        LOG_ERR("FuseArchiveMount: mkdtemp failed: %s", strerror(errno));
        return false;
    }
    m_mountPoint = tmpl;
    buildTree();
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
        fuse_unmount(f);
        fuse_destroy(f);
    });

    LOG_DBG("FuseArchiveMount: mounted at %s", m_mountPoint.c_str());
    return true;
}

void FuseArchiveMount::unmount()
{
    if (m_session)
    {
        fuse_exit(static_cast<struct fuse*>(m_session));
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
