#include "ModEngine.h"
#include "Logging.h"
#include "WavHeader.h"

#include <xmp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// Safely copy a fixed-size char array (possibly not null-terminated) into a std::string.
static std::string nameFromFixed(const char* buf, size_t maxLen)
{
    auto end = std::find(buf, buf + maxLen, '\0');
    return std::string(buf, static_cast<size_t>(end - buf));
}

static std::string sanitizeEntryName(const std::string& s)
{
    std::string r = s;
    for (auto& c : r) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '*'
            || c == '?' || c == '<' || c == '>' || c == '|'
            || (static_cast<unsigned char>(c) < 0x20) || c == '\x7f')
            c = '_';
    }
    return r;
}

ModEngine::~ModEngine()
{
    ModEngine::Close();
}

bool ModEngine::Open(std::string_view path)
{
    Close();
    m_path = path;

    std::ifstream in(std::string(path).c_str(), std::ios::binary | std::ios::ate);
    if (!in)
    {
        LOG_ERR("Tracker: failed to open %s", std::string(path).c_str());
        return false;
    }

    auto fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0);

    std::vector<uint8_t> fileData(fileSize);
    if (!in.read(reinterpret_cast<char*>(fileData.data()), fileSize))
    {
        LOG_ERR("Tracker: failed to read %s", std::string(path).c_str());
        return false;
    }
    in.close();

    xmp_context ctx = xmp_create_context();
    if (!ctx)
    {
        LOG_ERR("Tracker: failed to create context");
        return false;
    }

    int ret = xmp_load_module_from_memory(ctx, fileData.data(),
                                           static_cast<long>(fileData.size()));
    if (ret != 0)
    {
        // Map libxmp errors to human-readable form
        const char* errMsg = "unknown";
        switch (ret)
        {
        case XMP_ERROR_INTERNAL: errMsg = "internal"; break;
        case XMP_ERROR_FORMAT:   errMsg = "unsupported format"; break;
        case XMP_ERROR_LOAD:     errMsg = "load error"; break;
        case XMP_ERROR_DEPACK:   errMsg = "depack error"; break;
        case XMP_ERROR_SYSTEM:   errMsg = "system error"; break;
        case XMP_ERROR_INVALID:  errMsg = "invalid param"; break;
        case XMP_ERROR_STATE:    errMsg = "state error"; break;
        }
        LOG_WARN("Tracker: libxmp could not parse %s: %s (error %d)",
                 std::string(path).c_str(), errMsg, ret);
        xmp_free_context(ctx);
        return false;
    }

    m_ctx = static_cast<void*>(ctx);

    struct xmp_module_info mi;
    xmp_get_module_info(ctx, &mi);

    auto* mod = mi.mod;
    if (!mod)
    {
        LOG_ERR("Tracker: libxmp returned null module info");
        Close();
        return false;
    }

    LOG_DBG("Tracker: %s has %d samples, %d instruments",
            m_path.c_str(), mod->smp, mod->ins);

    m_samples.reserve(static_cast<size_t>(mod->smp));
    m_entries.reserve(static_cast<size_t>(mod->smp));

    for (int i = 0; i < mod->smp; ++i)
    {
        auto& xs = mod->xxs[i];
        if (xs.len <= 0 || !xs.data) continue;

        SampleEntry se;
        se.index = i;
        if (i < mod->ins && mod->xxi[i].name[0])
            se.name = nameFromFixed(mod->xxi[i].name, 32);
        else if (xs.name[0])
            se.name = nameFromFixed(xs.name, 32);
        se.data = xs.data;
        se.length = xs.len;
        se.bits = (xs.flg & XMP_SAMPLE_16BIT) ? 16 : 8;
        se.channels = (xs.flg & XMP_SAMPLE_STEREO) ? 2 : 1;

        std::string entryName;
        if (mod->smp > 1) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02d", i + 1);
            entryName = "Sample " + std::string(buf);
        } else {
            entryName = "Sample";
        }

        if (!se.name.empty())
        {
            entryName += " - ";
            entryName += se.name;
        }
        entryName += ".wav";
        entryName = sanitizeEntryName(entryName);

        ArchiveEntry ae;
        ae.name = entryName;
        ae.path = entryName;
        ae.size = sizeof(WavHeader)
            + static_cast<uint64_t>(se.length);
        ae.packedSize = 0;
        ae.isDirectory = false;

        char method[32];
        std::snprintf(method, sizeof(method), "PCM %d-bit %dch", se.bits, se.channels);
        ae.compressionMethod = method;

        m_samples.push_back(std::move(se));
        m_entries.push_back(std::move(ae));
    }

    m_formatLabel = formatLabelFromType();

    LOG_DBG("Tracker: opened %s (%zu entries, %s)",
            m_path.c_str(), m_entries.size(), m_formatLabel.c_str());
    return true;
}

std::string ModEngine::formatLabelFromType()
{
    if (!m_ctx) return "Tracker";

    struct xmp_module_info mi;
    xmp_get_module_info(static_cast<xmp_context>(m_ctx), &mi);

    if (mi.mod && mi.mod->type[0])
        return "Tracker (" + std::string(mi.mod->type) + ")";

    return "Tracker";
}

void ModEngine::Close()
{
    if (m_ctx)
    {
        xmp_release_module(static_cast<xmp_context>(m_ctx));
        xmp_free_context(static_cast<xmp_context>(m_ctx));
        m_ctx = nullptr;
    }
    m_samples.clear();
    m_entries.clear();
    m_path.clear();
}

const std::vector<ArchiveEntry>& ModEngine::ListContents()
{
    return m_entries;
}

std::vector<uint8_t> ModEngine::sampleToWav(const SampleEntry& sample)
{
    if (!sample.data || sample.length <= 0)
        return {};

    int bits = sample.bits;
    if (bits != 8 && bits != 16) bits = 16;

    WavHeader hdr;
    hdr.numChannels = static_cast<uint16_t>(sample.channels);
    hdr.bitsPerSample = static_cast<uint16_t>(bits);
    hdr.blockAlign = static_cast<uint16_t>(sample.channels * (bits / 8));
    hdr.byteRate = 44100 * hdr.blockAlign;
    hdr.dataSize = static_cast<uint32_t>(sample.length);
    hdr.fileSize = static_cast<uint32_t>(sizeof(WavHeader) - 8 + sample.length);

    std::vector<uint8_t> result(sizeof(WavHeader) + static_cast<size_t>(sample.length));
    std::memcpy(result.data(), &hdr, sizeof(WavHeader));

    std::memcpy(result.data() + sizeof(WavHeader), sample.data,
                static_cast<size_t>(sample.length));

    // libxmp stores 8-bit samples as signed (-128..127), WAV expects unsigned (0..255).
    // 16-bit samples are already signed little-endian in both libxmp (on x86) and WAV.
    if (bits == 8)
    {
        auto* p = result.data() + sizeof(WavHeader);
        for (int32_t i = 0; i < sample.length; ++i)
            p[i] = static_cast<uint8_t>(static_cast<int>(p[i]) + 128);
    }

    return result;
}

std::vector<uint8_t> ModEngine::ReadFile(std::string_view entryName)
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].name == entryName || m_entries[i].path == entryName)
            return sampleToWav(m_samples[i]);
    }
    LOG_WARN("Tracker: entry '%.*s' not found", (int)entryName.size(), entryName.data());
    return {};
}

bool ModEngine::ExtractAll(std::string_view destPath)
{
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (!isSafeEntryName(m_entries[i].name)) { LOG_WARN("Tracker: skipping unsafe entry '%s'", m_entries[i].name.c_str()); continue; }
        fs::path outPath = fs::path(destPath) / m_entries[i].name;
        fs::create_directories(outPath.parent_path());

        auto data = sampleToWav(m_samples[i]);
        if (data.empty()) continue;

        std::ofstream out(outPath, std::ios::binary);
        if (!out)
        {
            LOG_ERR("Tracker: cannot create %s", outPath.string().c_str());
            return false;
        }
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return true;
}

bool ModEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> cancelFlag)
{
    if (!m_ctx) return false;

    struct xmp_module_info mi;
    xmp_get_module_info(static_cast<xmp_context>(m_ctx), &mi);

    int total = mi.mod ? mi.mod->smp : 0;
    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag && cancelFlag()) return false;
        if (progressCallback)
            progressCallback(i, total);
    }
    return true;
}

std::string ModEngine::archiveComment() const
{
    if (!m_ctx) return {};

    struct xmp_module_info mi;
    xmp_get_module_info(static_cast<xmp_context>(m_ctx), &mi);

    std::string result;

    if (mi.mod)
    {
        if (mi.mod->name[0])
        {
            result = "Title: ";
            result += mi.mod->name;
            result += '\n';
        }
        if (mi.mod->type[0])
        {
            result += "Type: ";
            result += mi.mod->type;
            result += '\n';
        }

        bool insHeader = false;
        for (int i = 0; i < mi.mod->ins; ++i)
        {
            if (!mi.mod->xxi[i].name[0]) continue;
            if (!insHeader) { result += "Instruments:\n"; insHeader = true; }
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02d", i + 1);
            result += "  ";
            result += buf;
            result += ": ";
            result += mi.mod->xxi[i].name;
            result += '\n';
        }

        bool smpHeader = false;
        for (int i = 0; i < mi.mod->smp; ++i)
        {
            if (!mi.mod->xxs[i].name[0]) continue;
            bool already = (i < mi.mod->ins && mi.mod->xxi[i].name[0]);
            if (already) continue;
            if (!smpHeader) { result += "Samples:\n"; smpHeader = true; }
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02d", i + 1);
            result += "  ";
            result += buf;
            result += ": ";
            result += mi.mod->xxs[i].name;
            result += '\n';
        }
    }

    if (mi.comment && mi.comment[0])
    {
        if (!result.empty() && result.back() != '\n')
            result += '\n';
        result += '\n';
        result += mi.comment;
    }

    return result;
}
