#ifndef ZIPFX_ADF_ENGINE_H
#define ZIPFX_ADF_ENGINE_H

#include "ArchiveEngine.h"

#include <string>
#include <vector>

class AdfEngine : public ArchiveEngine
{
public:
    AdfEngine();
    ~AdfEngine() override;

    bool Open(std::string_view path) override;
    bool Create(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    void cancelExtract() override { m_extractCancelled = true; }

    std::string_view FormatName() const override { return "ADF"; }
    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_isOpen; }

private:
    int findEntry(std::string_view name) const;
    void walkDir(void* vol, int sector, const std::string& prefix);
    bool ensureDir(void* vol, const std::string& path);

    void* m_dev = nullptr;
    void* m_vol = nullptr;
    std::string m_path;
    bool m_isOpen = false;
    bool m_modified = false;
    std::vector<ArchiveEntry> m_entries;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
