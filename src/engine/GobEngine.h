#ifndef ZIPFX_GOB_ENGINE_H
#define ZIPFX_GOB_ENGINE_H

#include "FlatArchiveEngine.h"

class GobEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "GOB"; }

private:
    bool doSave(std::ofstream& f) override;
};

#endif
