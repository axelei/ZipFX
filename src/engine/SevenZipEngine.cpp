#include "SevenZipEngine.h"

#include "Logging.h"

#include <archive.h>

// ── Lifecycle ──────────────────────────────────────────────────────────
bool SevenZipEngine::Open(std::string_view path)
{
    return OpenInternal(path);
}

void SevenZipEngine::RegisterFormat(struct archive* a)
{
    archive_read_support_format_7zip(a);
}

void SevenZipEngine::PostProcessEntry(ArchiveEntry& entry)
{
    entry.compressionMethod = "LZMA2";
}
