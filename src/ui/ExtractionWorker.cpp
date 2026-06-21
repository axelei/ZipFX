#include "ExtractionWorker.h"

#include <wx/log.h>
#include <wx/window.h>
#include <wx/filename.h>
#include <wx/event.h>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEntry.h"

#include <filesystem>
namespace fs = std::filesystem;

wxDEFINE_EVENT(wxEVT_EXTRACT_PROGRESS, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_EXTRACT_DONE,     wxThreadEvent);

ExtractionWorker::ExtractionWorker(wxWindow* parent, ArchiveEngine* engine,
    std::vector<ArchiveEntry> entries,
    const wxString& destPath, bool preserveStructure)
    : m_parent(parent)
    , m_engine(engine)
    , m_entries(std::move(entries))
    , m_destPath(destPath)
    , m_preserveStructure(preserveStructure)
{
}

void ExtractionWorker::Start()
{
    m_thread = std::thread(&ExtractionWorker::Run, this);
}

void ExtractionWorker::Cancel()
{
    m_cancelled = true;
}

void ExtractionWorker::Run()
{
    int total = static_cast<int>(m_entries.size());
    int ok = 0, skipped = 0;

    for (int i = 0; i < total; ++i)
    {
        if (m_cancelled) break;

        const auto& entry = m_entries[i];
        wxString name = wxString::FromUTF8(entry.name.c_str());

        // Build destination path
        wxString relName = m_preserveStructure
            ? wxString::FromUTF8(entry.path.c_str())
            : name.AfterLast('/');
        wxString destFile = m_destPath + "/" + relName;

        // Skip directories
        if (entry.isDirectory)
        {
            wxFileName::Mkdir(destFile, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            continue;
        }

        // Create parent directory
        wxFileName::Mkdir(wxFileName(destFile).GetPath(),
                          wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        if (m_engine->Extract(entry.path, destFile.ToStdString()))
        {
            ++ok;
        }
        else
        {
            wxLogWarning("ExtractWorker: failed %s", name);
            ++skipped;
        }

        // Post progress to main thread
        wxThreadEvent* evt = new wxThreadEvent(wxEVT_EXTRACT_PROGRESS);
        evt->SetInt(i + 1);
        evt->SetExtraLong(total);
        evt->SetString(name);
        wxQueueEvent(m_parent, evt);
    }

    // Post completion
    wxThreadEvent* done = new wxThreadEvent(wxEVT_EXTRACT_DONE);
    done->SetInt(ok);
    done->SetExtraLong(skipped);
    wxQueueEvent(m_parent, done);
}
