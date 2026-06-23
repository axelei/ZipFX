#ifndef ZIPFX_VPK_ENGINE_H
#define ZIPFX_VPK_ENGINE_H

#include "FlatArchiveEngine.h"

class VpkEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "VPK"; }

private:
    bool doSave(std::ofstream& f) override;
};

#endif
