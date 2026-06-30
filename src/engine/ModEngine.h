#ifndef ZIPFX_MOD_ENGINE_H
#define ZIPFX_MOD_ENGINE_H

#include "ArchiveEngine.h"

class ModEngine : public ArchiveEngine
{
public:
    ~ModEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;
    const std::vector<ArchiveEntry>& ListContents() override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return m_formatLabel; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_ctx != nullptr; }
    bool supportsArchiveComment() const override { return true; }
    std::string archiveComment() const override;

private:
    struct SampleEntry {
        int32_t index;
        std::string name;
        const unsigned char* data = nullptr;
        int32_t length = 0;
        int32_t bits = 8;
        int32_t channels = 1;
    };

    std::string formatLabelFromType();
    std::vector<uint8_t> sampleToWav(const SampleEntry& sample);

    void* m_ctx = nullptr;
    std::string m_path;
    std::string m_formatLabel = "Tracker";
    std::vector<SampleEntry> m_samples;
    std::vector<ArchiveEntry> m_entries;
};

#endif
