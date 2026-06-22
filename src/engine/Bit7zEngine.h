#ifndef ZIPFX_BIT7Z_ENGINE_H
#define ZIPFX_BIT7Z_ENGINE_H

#include "ArchiveEngine.h"

#include <memory>
#include <string>
#include <vector>

namespace bit7z {
class Bit7zLibrary;
class BitArchiveReader;
}

class Bit7zEngine : public ArchiveEngine
{
public:
    Bit7zEngine();
    ~Bit7zEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return "Bit7z"; }
    bool SupportsCreation() const override { return false; }
    bool IsOpen() const override { return m_isOpen; }

private:
    int findEntry(std::string_view name) const;

    std::unique_ptr<bit7z::Bit7zLibrary> m_lib;
    std::unique_ptr<bit7z::BitArchiveReader> m_reader;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;
};

#endif
