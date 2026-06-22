#ifndef ZIPFX_LIBARCHIVE_ENGINE_H
#define ZIPFX_LIBARCHIVE_ENGINE_H

#include "ArchiveEngine.h"

#include <functional>
#include <vector>

struct archive;
struct archive_entry;

class LibarchiveEngine : public ArchiveEngine
{
public:
    using FormatRegistrar = int (*)(struct archive*);

    LibarchiveEngine(std::vector<FormatRegistrar> registrars,
                     const char* formatName,
                     bool supportsCreation = false,
                     const char* compressionMethod = nullptr);
    ~LibarchiveEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;
    std::vector<ArchiveEntry> ListContents() override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName() const override { return m_formatName; }
    bool SupportsCreation() const override { return m_supportsCreation; }
    bool IsOpen() const override { return m_isOpen; }

private:
    bool openArchive(std::string_view path);
    bool LoadEntries();
    void registerFormat(struct archive* a);

    struct archive* m_archive = nullptr;
    std::string m_path;
    std::string m_formatName;
    std::string m_compressionMethod;
    bool m_isOpen = false;
    bool m_supportsCreation = false;
    std::vector<FormatRegistrar> m_registrars;
    std::vector<ArchiveEntry> m_entries;
};

#endif
