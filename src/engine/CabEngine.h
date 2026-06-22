#ifndef ZIPFX_CAB_ENGINE_H
#define ZIPFX_CAB_ENGINE_H

#include "LibarchiveEngine.h"

class CabEngine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "CAB"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
