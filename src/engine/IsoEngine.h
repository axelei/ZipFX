#ifndef ZIPFX_ISO_ENGINE_H
#define ZIPFX_ISO_ENGINE_H

#include "LibarchiveEngine.h"

class IsoEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "ISO"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
