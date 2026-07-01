#ifdef __APPLE__

#include "DmgEngine.h"
#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Maximum bytes loaded into memory by ReadFile (256 MB).
static constexpr size_t kMaxReadBytes = 256u * 1024u * 1024u;

// Run /usr/bin/hdiutil with the given arguments, no shell involved.
// stdout and stderr are redirected to /dev/null.
extern char** environ;
static int runHdiutil(const std::vector<std::string>& args)
{
    std::vector<const char*> argv;
    argv.push_back("/usr/bin/hdiutil");
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid;
    int rc = posix_spawn(&pid, "/usr/bin/hdiutil", &actions, nullptr,
                         const_cast<char* const*>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Return a writable per-user temp directory (private on macOS via $TMPDIR).
static std::string userTempDir()
{
    const char* t = std::getenv("TMPDIR");
    return (t && t[0]) ? std::string(t) : std::string("/tmp/");
}

DmgEngine::DmgEngine() = default;

DmgEngine::~DmgEngine()
{
    Close();
}

bool DmgEngine::Open(std::string_view path)
{
    m_path = std::string(path);
    if (!mountDmg())
        return false;

    walkDir(m_mountPoint, "");
    m_isOpen = true;
    return true;
}

void DmgEngine::Close()
{
    unmountDmg();
    m_entries.clear();
    m_isOpen = false;
}

bool DmgEngine::mountDmg()
{
    std::string tmplStr = userTempDir() + "zipfx-dmg-XXXXXX";
    std::vector<char> tmpl(tmplStr.begin(), tmplStr.end());
    tmpl.push_back('\0');

    if (!mkdtemp(tmpl.data())) {
        LOG_ERR("DmgEngine: mkdtemp failed");
        return false;
    }
    m_mountPoint = tmpl.data();

    int rc = runHdiutil({"attach", "-readonly", "-nobrowse", "-noautoopen",
                         "-mountpoint", m_mountPoint, m_path});
    if (rc != 0) {
        LOG_ERR("DmgEngine: hdiutil attach failed for %s", m_path.c_str());
        if (::rmdir(m_mountPoint.c_str()) != 0)
            LOG_WARN("DmgEngine: rmdir '%s' failed", m_mountPoint.c_str());
        m_mountPoint.clear();
        return false;
    }
    return true;
}

void DmgEngine::unmountDmg()
{
    if (m_mountPoint.empty()) return;
    runHdiutil({"detach", m_mountPoint, "-force"});
    if (::rmdir(m_mountPoint.c_str()) != 0)
        LOG_WARN("DmgEngine: rmdir '%s' failed after detach", m_mountPoint.c_str());
    m_mountPoint.clear();
}

void DmgEngine::walkDir(const std::string& dirPath, const std::string& prefix)
{
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dirPath, ec)) {
        if (ec) break;

        const std::string name = e.path().filename().string();
        const std::string relPath = prefix.empty() ? name : prefix + "/" + name;

        ArchiveEntry ae;
        ae.name = relPath;
        ae.path = relPath;

        if (e.is_symlink(ec)) {
            continue; // skip aliases (e.g. Applications shortcut in app DMGs)
        } else if (e.is_directory(ec)) {
            ae.isDirectory = true;
            m_entries.push_back(ae);
            walkDir(e.path().string(), relPath);
        } else if (e.is_regular_file(ec)) {
            ae.isDirectory = false;
            ae.size = e.file_size(ec);
            ae.packedSize = ae.size;

            struct stat st{};
            if (::stat(e.path().c_str(), &st) == 0) {
                ae.permissions = st.st_mode & 0777u;
                ae.modified = std::chrono::system_clock::from_time_t(st.st_mtime);
            }

            ae.compressionMethod = "stored";
            m_entries.push_back(ae);
        }
    }
}

const std::vector<ArchiveEntry>& DmgEngine::ListContents()
{
    return m_entries;
}

bool DmgEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (m_mountPoint.empty()) return false;

    std::string src = m_mountPoint + "/" + std::string(entryName);
    std::string dst = std::string(destPath);

    std::error_code ec;
    for (const auto& e : m_entries) {
        if (e.name != entryName && e.path != entryName) continue;
        if (e.isDirectory) {
            fs::create_directories(dst, ec);
            return !ec;
        }
        fs::create_directories(fs::path(dst).parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }
    return false;
}

bool DmgEngine::ExtractAll(std::string_view destPath)
{
    if (m_mountPoint.empty()) return false;

    bool ok = true;
    std::error_code ec;

    for (const auto& e : m_entries) {
        if (m_openCancelled) break;

        std::string src = m_mountPoint + "/" + e.path;
        std::string dst = std::string(destPath) + "/" + e.path;

        if (m_extractProgressCb) {
            ExtractProgressInfo info;
            info.fileName = e.name;
            info.totalBytes = e.size;
            m_extractProgressCb(info);
        }

        if (e.isDirectory) {
            fs::create_directories(dst, ec);
        } else {
            fs::create_directories(fs::path(dst).parent_path(), ec);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                LOG_WARN("DmgEngine: failed to copy %s: %s", src.c_str(), ec.message().c_str());
                ok = false;
            }
        }
    }
    return ok;
}

std::vector<uint8_t> DmgEngine::ReadFile(std::string_view entryName)
{
    if (m_mountPoint.empty()) return {};

    // Validate entryName against the known entry list (prevents path traversal).
    bool found = false;
    for (const auto& e : m_entries) {
        if (!e.isDirectory && (e.name == entryName || e.path == entryName)) {
            found = true;
            break;
        }
    }
    if (!found) return {};

    std::string src = m_mountPoint + "/" + std::string(entryName);
    std::ifstream f(src, std::ios::binary);
    if (!f) return {};

    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return {};

    // Cap in-memory reads to avoid exhausting RAM on large mounted files.
    if (static_cast<size_t>(sz) > kMaxReadBytes) {
        LOG_WARN("DmgEngine: ReadFile truncated '%s' to %zu MB (file is %lld bytes)",
                 std::string(entryName).c_str(),
                 kMaxReadBytes / (1024 * 1024),
                 static_cast<long long>(sz));
        sz = static_cast<std::streampos>(kMaxReadBytes);
    }

    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

bool DmgEngine::TestIntegrity(
    std::function<void(int, int)> /*progressCallback*/,
    std::function<bool()> /*cancelFlag*/)
{
    if (m_path.empty()) return false;
    return runHdiutil({"verify", "-quiet", m_path}) == 0;
}

#endif // __APPLE__
