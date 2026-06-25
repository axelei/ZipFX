#ifndef ZIPFX_RFF_ENGINE_H
#define ZIPFX_RFF_ENGINE_H

#include "FlatArchiveEngine.h"

class RffEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "RFF"; }

private:
    bool doSave(std::ofstream& f) override;
};

#endif
