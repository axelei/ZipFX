#ifndef ZIPFX_POD_ENGINE_H
#define ZIPFX_POD_ENGINE_H

#include "FlatArchiveEngine.h"

class PodEngine : public FlatArchiveEngine
{
public:
    bool Open(std::string_view path) override;
    std::string_view FormatName() const override { return "POD"; }

    std::string archiveComment() const override { return m_comment; }

private:
    bool doSave(std::ofstream& f) override;
    std::string m_comment;
};

#endif
