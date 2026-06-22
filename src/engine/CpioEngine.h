#ifndef ZIPFX_CPIO_ENGINE_H
#define ZIPFX_CPIO_ENGINE_H

#include "LibarchiveEngine.h"

class CpioEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "CPIO"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
