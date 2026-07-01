#ifndef ZIPFX_DMG_ENGINE_H
#define ZIPFX_DMG_ENGINE_H

#ifdef __APPLE__

#include "ArchiveEngine.h"
#include <string>
#include <vector>

// Reads Apple Disk Images (.dmg / UDIF format) by mounting via hdiutil and
// walking the resulting HFS+/APFS filesystem.  macOS-only.
class DmgEngine : public ArchiveEngine
{
public:
    DmgEngine();
    ~DmgEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return "DMG"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    bool mountDmg();
    void unmountDmg();
    void walkDir(const std::string& dirPath, const std::string& prefix);

    std::string m_path;
    std::string m_mountPoint;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;
};

#endif // __APPLE__
#endif // ZIPFX_DMG_ENGINE_H
