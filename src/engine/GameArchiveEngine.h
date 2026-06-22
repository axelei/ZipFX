#ifndef ZIPFX_GAME_ARCHIVE_ENGINE_H
#define ZIPFX_GAME_ARCHIVE_ENGINE_H

#include "ArchiveEngine.h"

#include <cstdint>
#include <string>
#include <vector>

enum class GameFormat { Wad, Pak, Grp };

class GameArchiveEngine : public ArchiveEngine
{
public:
    GameArchiveEngine(GameFormat fmt, const char* formatName);
    ~GameArchiveEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return m_formatName; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    int findEntry(std::string_view name) const;
    std::vector<uint8_t> readEntryData(uint32_t offset, uint32_t size) const;

    struct Entry { std::string name; uint32_t offset = 0; uint32_t size = 0; };

    GameFormat m_format;
    std::string m_formatName;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<Entry> m_entries;
};

#endif
