#include "RarEngine.h"

#include "Logging.h"

#include <archive.h>

// ── Lifecycle ──────────────────────────────────────────────────────────
bool RarEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void RarEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
}
