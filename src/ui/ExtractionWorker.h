#ifndef ZIPFX_EXTRACTION_WORKER_H
#define ZIPFX_EXTRACTION_WORKER_H

#include <wx/wx.h>
#include <wx/event.h>
#include <wx/thread.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class ArchiveEngine;
struct ArchiveEntry;

// Custom events for background extraction progress
wxDECLARE_EVENT(wxEVT_EXTRACT_PROGRESS, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_EXTRACT_DONE,     wxThreadEvent);

// Background thread that extracts files and posts progress to the main thread.
class ExtractionWorker : public wxEvtHandler
{
public:
    ExtractionWorker(wxWindow* parent, ArchiveEngine* engine,
                     std::vector<ArchiveEntry> entries,
                     const wxString& destPath, bool preserveStructure);

    void Start();
    void Cancel();
    bool IsRunning() const { return m_thread.joinable(); }

private:
    void Run();

    wxWindow*                 m_parent;
    ArchiveEngine*            m_engine;
    std::vector<ArchiveEntry> m_entries;
    wxString                  m_destPath;
    bool                      m_preserveStructure;

    std::thread               m_thread;
    std::atomic<bool>         m_cancelled{false};
};

#endif
