#ifndef ZIPFX_LIBARCHIVE_ENGINE_H
#define ZIPFX_LIBARCHIVE_ENGINE_H

#include "ArchiveEngine.h"

struct archive;
struct archive_entry;

class LibarchiveEngine : public ArchiveEngine
{
public:
    ~LibarchiveEngine() override;

    // Inherited from ArchiveEngine — shared implementations
    void Close() override;
    std::vector<ArchiveEntry> ListContents() override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    bool TestIntegrity() override;

    bool IsOpen() const override { return m_isOpen; }
    bool SupportsCreation() const override { return false; }

protected:
    struct archive* m_archive = nullptr;
    std::string m_path;
    bool m_isOpen = false;
    std::vector<ArchiveEntry> m_entries;

    bool OpenInternal(std::string_view path);
    bool LoadEntries();

    // Subclasses register their format here
    virtual void RegisterFormat(struct archive* a) = 0;

    // Hook for subclass-specific entry fields
    virtual void PostProcessEntry(ArchiveEntry& entry) {}
};

#endif
