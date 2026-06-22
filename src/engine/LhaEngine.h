#ifndef ZIPFX_LHA_ENGINE_H
#define ZIPFX_LHA_ENGINE_H

#include "LibarchiveEngine.h"

class LhaEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "LHA"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
