#ifndef ZIPFX_HOG_ENGINE_H
#define ZIPFX_HOG_ENGINE_H

#include "FlatArchiveEngine.h"

class HogEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "HOG"; }

private:
    bool doSave(std::ofstream& f) override;
};

#endif
