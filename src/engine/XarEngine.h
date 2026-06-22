#ifndef ZIPFX_XAR_ENGINE_H
#define ZIPFX_XAR_ENGINE_H

#include "LibarchiveEngine.h"

class XarEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "XAR"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
