#include "Rar5Engine.h"

#include "Logging.h"

#include <archive.h>

bool Rar5Engine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void Rar5Engine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_rar5(a);
}
