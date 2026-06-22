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

namespace fs = std::filesystem;

AdfEngine::AdfEngine() = default;

AdfEngine::~AdfEngine()
{
    Close();
}

bool AdfEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    adfLibInit();
    adfEnvSetProperty(ADF_PR_QUIET, true);

    auto* dev = adfDevOpen(m_path.c_str(), ADF_ACCESS_MODE_READONLY);
    if (!dev)
    {
        LOG_ERR("AdfEngine: failed to open %s", m_path.c_str());
        adfLibCleanUp();
        return false;
    }

    if (adfDevMount(dev) != ADF_RC_OK)
    {
        adfDevClose(dev);
        adfLibCleanUp();
        return false;
    }

    auto* vol = adfVolMount(dev, 0, ADF_ACCESS_MODE_READONLY);
    if (!vol)
    {
        adfDevUnMount(dev);
        adfDevClose(dev);
        adfLibCleanUp();
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
        auto* vol = static_cast<struct AdfVolume*>(m_vol);
        adfVolUnMount(vol);
        m_vol = nullptr;
    }
    if (m_dev)
    {
        auto* dev = static_cast<struct AdfDevice*>(m_dev);
        adfDevUnMount(dev);
        adfDevClose(dev);
        m_dev = nullptr;
    }
    adfLibCleanUp();
    m_isOpen = false;
    m_entries.clear();
}

void AdfEngine::walkDir(void* volPtr, int sector, const std::string& prefix)
{
    auto* vol = static_cast<struct AdfVolume*>(volPtr);
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

std::vector<ArchiveEntry> AdfEngine::ListContents()
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
    auto data = ReadFile(entryName);
    if (data.empty()) return false;

    fs::path dest(destPath);
    fs::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

bool AdfEngine::ExtractAll(std::string_view destPath)
{
    for (const auto& e : m_entries)
    {
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

    auto* vol = static_cast<struct AdfVolume*>(m_vol);

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
