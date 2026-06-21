#include "ExtractionWorker.h"

#include <wx/log.h>
#include <wx/filename.h>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEntry.h"

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
    m_progressTotal = static_cast<int>(m_entries.size());
}

ExtractionWorker::~ExtractionWorker()
{
    Cancel();
    Resume(); // wake up in case paused
    if (m_thread.joinable())
        m_thread.join();
}

void ExtractionWorker::Start()
{
    m_thread = std::thread(&ExtractionWorker::Run, this);
}

void ExtractionWorker::Cancel()
{
    m_cancelled = true;
}

void ExtractionWorker::Pause()
{
    m_paused = true;
}

void ExtractionWorker::Resume()
{
    m_paused = false;
    m_pauseCv.notify_all();
}

void ExtractionWorker::Run()
{
    int total = static_cast<int>(m_entries.size());
    int ok = 0, skipped = 0;

    try
    {
        for (int i = 0; i < total; ++i)
        {
            // Check cancel
            if (m_cancelled) break;

            // Check pause
            if (m_paused)
            {
                std::unique_lock<std::mutex> lock(m_pauseMtx);
                m_pauseCv.wait(lock, [this] { return !m_paused || m_cancelled; });
                if (m_cancelled) break;
            }

            const auto& entry = m_entries[i];
            wxString name = wxString::FromUTF8(entry.name.c_str());

            wxString relName = m_preserveStructure
                ? wxString::FromUTF8(entry.path.c_str())
                : name.AfterLast('/');
            wxString destFile = m_destPath + "/" + relName;

            if (entry.isDirectory)
            {
                wxFileName::Mkdir(destFile, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
                m_progressCount = i + 1;
                continue;
            }

            wxFileName::Mkdir(wxFileName(destFile).GetPath(),
                              wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

            if (m_engine->Extract(entry.path, destFile.ToStdString()))
                ++ok;
            else
                wxLogWarning("ExtractWorker: failed %s", name);

            m_progressCount = i + 1;
        }
    }
    catch (std::exception& e)
    {
        wxLogError("ExtractWorker: exception: %s", e.what());
    }
    catch (...)
    {
        wxLogError("ExtractWorker: unknown exception");
    }

    m_finished = true;

    wxLogMessage("ExtractWorker done: %d OK, %d skipped", ok, skipped);
}
