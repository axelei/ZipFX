#include "IsoEngine.h"

#include <archive.h>

bool IsoEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void IsoEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_iso9660(a);
}
