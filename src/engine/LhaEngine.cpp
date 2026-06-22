#include "LhaEngine.h"

#include <archive.h>

bool LhaEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void LhaEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_lha(a);
}
