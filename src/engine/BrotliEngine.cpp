#include "BrotliEngine.h"
#include "Logging.h"

#include <brotli/decode.h>

#include <array>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

bool BrotliEngine::Open(std::string_view path)
{
    Close();
    m_path = std::string(path);

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return false;

    f.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(f.tellg());

    // Derive entry name by stripping the .br extension
    fs::path p(m_path);
    std::string stem = p.stem().string();
    if (stem.empty()) stem = p.filename().string();

    ArchiveEntry ae;
    ae.name        = stem;
    ae.path        = stem;
    ae.size        = 0;  // unknown uncompressed size
    ae.packedSize  = fileSize;
    ae.isDirectory = false;
    ae.compressionMethod = "brotli";
    m_entries.push_back(std::move(ae));

    m_isOpen = true;
    return true;
}

void BrotliEngine::Close()
{
    m_isOpen = false;
    m_entries.clear();
    m_path.clear();
}

const std::vector<ArchiveEntry>& BrotliEngine::ListContents()
{
    return m_entries;
}

std::vector<uint8_t> BrotliEngine::decompress(const std::vector<uint8_t>& input, size_t maxBytes)
{
    BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) return {};

    std::vector<uint8_t> output;
    const uint8_t* next_in = input.data();
    size_t avail_in = input.size();

    std::array<uint8_t, 65536> buf;
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;

    while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
    {
        uint8_t* next_out = buf.data();
        size_t avail_out = buf.size();
        result = BrotliDecoderDecompressStream(
            state, &avail_in, &next_in, &avail_out, &next_out, nullptr);
        size_t produced = buf.size() - avail_out;
        if (output.size() + produced > maxBytes)
            produced = maxBytes - output.size();
        output.insert(output.end(), buf.begin(), buf.begin() + produced);
        if (m_extractCancelled) { BrotliDecoderDestroyInstance(state); return {}; }
        if (output.size() >= maxBytes) break;
    }

    BrotliDecoderDestroyInstance(state);

    if (result != BROTLI_DECODER_RESULT_SUCCESS && result != BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
    {
        LOG_WARN("BrotliEngine: decompression failed");
        return {};
    }

    return output;
}

std::vector<uint8_t> BrotliEngine::ReadFile(std::string_view entryName)
{
    if (!m_isOpen || m_entries.empty()) return {};
    if (entryName != m_entries[0].name && entryName != m_entries[0].path) return {};
    m_extractCancelled = false;

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> input(std::istreambuf_iterator<char>(f), {});
    return decompress(input, SIZE_MAX);
}

std::vector<uint8_t> BrotliEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_isOpen || m_entries.empty()) return {};
    if (entryName != m_entries[0].name && entryName != m_entries[0].path) return {};

    std::ifstream f(m_path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> input(std::istreambuf_iterator<char>(f), {});
    return decompress(input, maxBytes);
}

bool BrotliEngine::Extract(std::string_view entryName, std::string_view destPath)
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

bool BrotliEngine::ExtractAll(std::string_view destPath)
{
    if (!m_isOpen || m_entries.empty()) return false;
    return Extract(m_entries[0].name, (fs::path(destPath) / m_entries[0].name).string());
}

bool BrotliEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()> /*cancelFlag*/)
{
    if (!m_isOpen) return false;
    if (progressCallback) progressCallback(0, 1);
    auto data = ReadFile(m_entries.empty() ? "" : m_entries[0].name);
    if (progressCallback) progressCallback(1, 1);
    return !data.empty();
}
