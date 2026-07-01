#ifdef __APPLE__

#include "DmgEngine.h"
#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Single-quote–escape a path for /bin/sh.
static std::string shellq(const std::string& s)
{
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else            r += c;
    }
    r += "'";
    return r;
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
    char tmpl[] = "/tmp/zipfx-dmg-XXXXXX";
    if (!mkdtemp(tmpl)) {
        LOG_ERR("DmgEngine: mkdtemp failed");
        return false;
    }
    m_mountPoint = tmpl;

    std::string cmd = "hdiutil attach -readonly -nobrowse -noautoopen"
                      " -mountpoint " + shellq(m_mountPoint) +
                      " " + shellq(m_path) +
                      " > /dev/null 2>&1";

    if (std::system(cmd.c_str()) != 0) {
        LOG_ERR("DmgEngine: hdiutil attach failed for %s", m_path.c_str());
        ::rmdir(tmpl);
        m_mountPoint.clear();
        return false;
    }
    return true;
}

void DmgEngine::unmountDmg()
{
    if (m_mountPoint.empty()) return;
    std::string cmd = "hdiutil detach " + shellq(m_mountPoint) + " -force > /dev/null 2>&1";
    std::system(cmd.c_str());
    ::rmdir(m_mountPoint.c_str());
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

    std::string src = m_mountPoint + "/" + std::string(entryName);
    std::ifstream f(src, std::ios::binary);
    if (!f) return {};

    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return {};
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
    std::string cmd = "hdiutil verify -quiet " + shellq(m_path) + " > /dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

#endif // __APPLE__
