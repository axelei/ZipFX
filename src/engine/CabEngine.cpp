#include "CabEngine.h"

#include <archive.h>

bool CabEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void CabEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_cab(a);
}
