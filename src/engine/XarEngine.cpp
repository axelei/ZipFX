#include "XarEngine.h"

#include <archive.h>

bool XarEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void XarEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_xar(a);
}
