#ifndef ZIPFX_TARGZ_ENGINE_H
#define ZIPFX_TARGZ_ENGINE_H

#include "ArchiveEngine.h"

struct gzFile_s;
typedef struct gzFile_s* gzFile;

class TarGzEngine : public ArchiveEngine
{
public:
    ~TarGzEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;

    std::vector<ArchiveEntry> ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool Save() override;

    bool TestIntegrity() override;

    std::string_view FormatName() const override { return "TAR.GZ"; }
    bool SupportsCreation() const override { return true; }
    bool IsOpen() const override { return m_isOpen; }

private:
    gzFile m_gzFile = nullptr;
    std::string m_path;
    bool m_isOpen = false;
    bool m_modified = false;
    std::vector<ArchiveEntry> m_entries;
};

#endif
