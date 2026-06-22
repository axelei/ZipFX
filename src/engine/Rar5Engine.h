#ifndef ZIPFX_RAR5_ENGINE_H
#define ZIPFX_RAR5_ENGINE_H

#include "LibarchiveEngine.h"

class Rar5Engine : public LibarchiveEngine
{
public:
    bool Open(std::string_view path) override;

    std::string_view FormatName() const override { return "RAR5"; }
    bool SupportsCreation() const override { return false; }

protected:
    void RegisterFormat(struct archive* a) override;
};

#endif
