#ifndef ZIPFX_RAR_ENGINE_H
#define ZIPFX_RAR_ENGINE_H

#include "LibarchiveEngine.h"

class RarEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "RAR"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
