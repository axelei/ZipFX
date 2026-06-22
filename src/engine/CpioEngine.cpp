#include "CpioEngine.h"

#include <archive.h>

bool CpioEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void CpioEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_cpio(a);
}
