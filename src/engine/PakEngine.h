#ifndef ZIPFX_PAK_ENGINE_H
#define ZIPFX_PAK_ENGINE_H

#include "FlatArchiveEngine.h"

class PakEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "PAK"; }
};

#endif
