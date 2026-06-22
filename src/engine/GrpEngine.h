#ifndef ZIPFX_GRP_ENGINE_H
#define ZIPFX_GRP_ENGINE_H

#include "FlatArchiveEngine.h"

class GrpEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "GRP"; }
};

#endif
