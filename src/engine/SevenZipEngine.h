#ifndef ZIPFX_SEVENZIP_ENGINE_H
#define ZIPFX_SEVENZIP_ENGINE_H

#include "LibarchiveEngine.h"

class SevenZipEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "7z"; }
    bool SupportsCreation() const override { return true; }

protected:
    void RegisterFormat(struct archive* a) override;
    void PostProcessEntry(ArchiveEntry& entry) override;
};

#endif
