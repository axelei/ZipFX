#ifndef ZIPFX_WAD_ENGINE_H
#define ZIPFX_WAD_ENGINE_H

#include "FlatArchiveEngine.h"

class WadEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return m_fmtName; }

private:
    bool doSave(std::ofstream& f) override;
    std::string m_fmtName = "IWAD";
};

#endif
