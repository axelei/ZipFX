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
    bool Create(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool RenameEntry(std::string_view entryName, std::string_view newName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return "MPQ"; }
    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_isOpen; }

private:
    int findEntry(std::string_view name) const;
    void reloadEntries();
    void* rawHandle() const { return m_handle; }

    void* m_handle = nullptr;
    std::string m_path;
    bool m_isOpen = false;
    bool m_modified = false;
    std::vector<ArchiveEntry> m_entries;

    struct PendingAdd
    {
        std::string srcPath;
        std::string archivePath;
    };
    std::vector<PendingAdd> m_pendingAdds;
};

#endif
