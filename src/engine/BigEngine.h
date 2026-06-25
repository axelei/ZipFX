#ifndef ZIPFX_BIG_ENGINE_H
#define ZIPFX_BIG_ENGINE_H

#include "FlatArchiveEngine.h"

class BigEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "BIG"; }

private:
    bool doSave(std::ofstream& f) override;
};

#endif
