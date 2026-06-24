#ifndef ZIPFX_MPQ_ENGINE_H
#define ZIPFX_MPQ_ENGINE_H

#include "ArchiveEngine.h"

#include <string>
#include <vector>

class MpqEngine : public ArchiveEngine
{
public:
    MpqEngine();
    ~MpqEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return "MPQ"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    int findEntry(std::string_view name) const;

    void* m_handle = nullptr; // HANDLE (TMPQArchive*)
    std::string m_path;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;
};

#endif
