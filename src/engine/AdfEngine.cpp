#include "AdfEngine.h"

#include "Logging.h"

// ADFlib uses 'class' as a member name which is a C++ keyword.
// Workaround: rename it locally for the include.
#define class adf_class
#include <adflib.h>
#undef class

#include <filesystem>
#include <fstream>
#include <cstring>
#include <ctime>
#include <mutex>

namespace fs = std::filesystem;

static std::once_flag s_adfInitFlag;

AdfEngine::AdfEngine() = default;

AdfEngine::~AdfEngine()
{
    Close();
}

bool AdfEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    std::call_once(s_adfInitFlag, []() {
        adfLibInit();
        adfEnvSetProperty(ADF_PR_QUIET, true);
    });

    auto* dev = adfDevOpen(m_path.c_str(), ADF_ACCESS_MODE_READONLY);
    if (!dev)
    {
        LOG_ERR("AdfEngine: failed to open %s", m_path.c_str());
        return false;
    }

    if (adfDevMount(dev) != ADF_RC_OK)
    {
        adfDevClose(dev);
        return false;
    }

    auto* vol = adfVolMount(dev, 0, ADF_ACCESS_MODE_READONLY);
    if (!vol)
    {
        adfDevUnMount(dev);
        adfDevClose(dev);
        return false;
    }

    m_dev = dev;
    m_vol = vol;
    m_isOpen = true;

    walkDir(vol, vol->rootBlock, "");
    LOG_DBG("AdfEngine: opened %s (%zu entries)", m_path.c_str(), m_entries.size());
    return true;
}

void AdfEngine::Close()
{
    if (m_vol)
    {
        auto* vol = m_vol;
        adfVolUnMount(vol);
        m_vol = nullptr;
    }
    if (m_dev)
    {
        auto* dev = m_dev;
        adfDevUnMount(dev);
        adfDevClose(dev);
        m_dev = nullptr;
    }
    m_isOpen = false;
    m_entries.clear();
}

bool AdfEngine::Create(std::string_view path)
{
    Close();
    m_path = path;

    std::call_once(s_adfInitFlag, []() {
        adfLibInit();
        adfEnvSetProperty(ADF_PR_QUIET, true);
    });

    // Standard floppy geometry: 80 cylinders, 2 heads, 11 sectors = 880 KB
    auto* dev = adfDevCreate("dump", m_path.c_str(), 80, 2, 11);
    if (!dev)
    {
        LOG_ERR("AdfEngine: failed to create %s", m_path.c_str());
        return false;
    }

    if (adfCreateFlop(dev, "ZipFX", ADF_DOSFS_FFS) != ADF_RC_OK)
    {
        adfDevClose(dev);
        LOG_ERR("AdfEngine: failed to format %s", m_path.c_str());
        return false;
    }

    if (adfDevMount(dev) != ADF_RC_OK)
    {
        adfDevClose(dev);
        return false;
    }

    auto* vol = adfVolMount(dev, 0, ADF_ACCESS_MODE_READWRITE);
    if (!vol)
    {
        adfDevUnMount(dev);
        adfDevClose(dev);
        return false;
    }

    m_dev = dev;
    m_vol = vol;
    m_isOpen = true;
    m_modified = false;
    LOG_DBG("AdfEngine: created %s", m_path.c_str());
    return true;
}

bool AdfEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    if (!m_isOpen || !m_vol) return false;

    auto* vol = m_vol;

    // Read the source file
    std::ifstream src(std::string(srcPath), std::ios::binary | std::ios::ate);
    if (!src) return false;
    auto fileSize = src.tellg();
    if (fileSize < 0) return false;
    src.seekg(0);
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    src.read(reinterpret_cast<char*>(fileData.data()), fileData.size());
    if (!src) return false;

    // Navigate to parent directory, creating path components as needed
    std::string pathStr(archivePath);
    auto slash = pathStr.rfind('/');
    std::string dirPath = (slash != std::string::npos) ? pathStr.substr(0, slash) : "";
    std::string fileName = (slash != std::string::npos) ? pathStr.substr(slash + 1) : pathStr;

    if (!ensureDir(vol, dirPath))
    {
        LOG_ERR("AdfEngine: failed to create directory path %s", dirPath.c_str());
        return false;
    }

    // Write the file
    auto* file = adfFileOpen(vol, pathStr.c_str(), ADF_FILE_MODE_WRITE);
    if (!file)
    {
        LOG_ERR("AdfEngine: failed to create file %s", pathStr.c_str());
        return false;
    }

    if (!fileData.empty())
    {
        auto written = adfFileWrite(file, fileData.size(), fileData.data());
        if (written != fileData.size())
        {
            LOG_ERR("AdfEngine: short write for %s (%u of %zu)",
                    pathStr.c_str(), written, fileData.size());
            adfFileClose(file);
            return false;
        }
    }

    adfFileClose(file);
    m_modified = true;
    return true;
}

bool AdfEngine::RemoveEntry(std::string_view entryName)
{
    LOG_WARN("AdfEngine: RemoveEntry not supported");
    return false;
}

bool AdfEngine::Save()
{
    if (!m_modified) return true;

    // ADFlib writes changes immediately — just re-scan the directory tree
    auto* vol = m_vol;
    m_entries.clear();
    walkDir(vol, vol->rootBlock, "");
    m_modified = false;
    LOG_DBG("AdfEngine: saved %s (%zu entries)", m_path.c_str(), m_entries.size());
    return true;
}

bool AdfEngine::ensureDir(AdfVolume* vol, const std::string& path)
{
    if (path.empty()) return true;

    // Navigate to root first
    adfToRootDir(vol);

    // Split path into components and create each
    std::string remaining = path;
    std::string component;
    size_t pos;
    while ((pos = remaining.find('/')) != std::string::npos)
    {
        component = remaining.substr(0, pos);
        remaining = remaining.substr(pos + 1);

        if (component.empty()) continue;

        // Try to change into this directory; if it fails, create it
        if (adfChangeDir(vol, component.c_str()) != ADF_RC_OK)
        {
            auto parentSect = vol->curDirPtr;
            if (adfCreateDir(vol, parentSect, component.c_str()) != ADF_RC_OK)
            {
                LOG_ERR("AdfEngine: failed to create dir %s", component.c_str());
                return false;
            }
            if (adfChangeDir(vol, component.c_str()) != ADF_RC_OK)
                return false;
        }
    }

    // Last path component
    if (!remaining.empty())
    {
        if (adfChangeDir(vol, remaining.c_str()) != ADF_RC_OK)
        {
            auto parentSect = vol->curDirPtr;
            if (adfCreateDir(vol, parentSect, remaining.c_str()) != ADF_RC_OK)
            {
                LOG_ERR("AdfEngine: failed to create dir %s", remaining.c_str());
                return false;
            }
        }
    }

    // Navigate back to root so subsequent operations use full paths
    adfToRootDir(vol);
    return true;
}

void AdfEngine::walkDir(AdfVolume* vol, int sector, const std::string& prefix)
{
    auto* list = adfGetRDirEnt(vol, sector, true);
    if (!list) return;

    struct AdfList* node = list;
    while (node)
    {
        auto* entry = static_cast<struct AdfEntry*>(node->content);
        if (entry && entry->name)
        {
            ArchiveEntry ae;
            ae.name = prefix + entry->name;
            ae.path = ae.name;
            ae.size = entry->size;
            ae.isDirectory = (entry->type == ADF_ST_DIR);
            ae.packedSize = ae.size;

            if (entry->year > 0)
            {
                struct tm tm = {};
                tm.tm_year = entry->year - 1900;
                tm.tm_mon = entry->month - 1;
                tm.tm_mday = entry->days;
                tm.tm_hour = entry->hour;
                tm.tm_min = entry->mins;
                tm.tm_sec = entry->secs;
                tm.tm_isdst = -1;
                time_t t = mktime(&tm);
                ae.modified = std::chrono::system_clock::from_time_t(t);
            }

            m_entries.push_back(std::move(ae));
        }
        node = node->next;
    }

    adfFreeDirList(list);
}

const std::vector<ArchiveEntry>& AdfEngine::ListContents()
{
    return m_entries;
}

int AdfEngine::findEntry(std::string_view name) const
{
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].path == name)
            return static_cast<int>(i);
    return -1;
}

bool AdfEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_isOpen || !m_vol) return false;
    m_extractCancelled = false;

    if (!isSafeEntryName(std::string(entryName))) return false;

    auto* file = adfFileOpen(m_vol, std::string(entryName).c_str(), ADF_FILE_MODE_READ);
    if (!file) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    if (!out) { adfFileClose(file); return false; }

    constexpr int kChunk = 65536;
    uint8_t buf[kChunk];
    int n;
    while ((n = adfFileRead(file, kChunk, buf)) > 0)
    {
        if (m_extractCancelled) { adfFileClose(file); return false; }
        out.write(reinterpret_cast<const char*>(buf), n);
    }

    adfFileClose(file);
    return out.good();
}

bool AdfEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
        if (m_extractCancelled) { LOG_DBG("AdfEngine: extract cancelled"); return false; }
        if (!isSafeEntryName(e.name)) { LOG_WARN("AdfEngine: skipping unsafe entry '%s'", e.name.c_str()); continue; }
        if (e.isDirectory)
        {
            fs::create_directories(fs::path(destPath) / e.name);
            continue;
        }

        fs::path fullPath = fs::path(destPath) / e.name;
        fs::create_directories(fullPath.parent_path());

        if (!Extract(e.name, fullPath.string()))
        {
            LOG_ERR("AdfEngine: failed to extract %s", e.name.c_str());
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> AdfEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen || !m_vol) return {};

    auto* vol = m_vol;

    auto* file = adfFileOpen(vol, std::string(entryName).c_str(), ADF_FILE_MODE_READ);
    if (!file) return {};

    std::vector<uint8_t> buf(65536);
    std::vector<uint8_t> result;
    int n;
    while ((n = adfFileRead(file, static_cast<int>(buf.size()), buf.data())) > 0)
    {
        result.insert(result.end(), buf.begin(), buf.begin() + n);
    }

    adfFileClose(file);
    return result;
}

bool AdfEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_isOpen) return false;

    int total = static_cast<int>(m_entries.size());
    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback) progressCallback(i, total);
    }
    return true;
}
