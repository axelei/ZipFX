#include <wx/wx.h>
#include <wx/log.h>
#include <wx/wfstream.h>
#include <wx/datetime.h>

#include "ui/MainFrame.h"

class ZipFXApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxInitAllImageHandlers();

#ifdef _DEBUG
        // 1. Console logging (stderr) — chained so error dialogs still appear
        new wxLogChain(new wxLogStream(&std::clog));

        // 2. File logging  — chained on top, so messages reach both files and console
        wxString logDir  = wxStandardPaths::Get().GetTempDir() + "/ZipFX/logs";
        wxFileName::Mkdir(logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        wxString logPath = logDir + "/" +
            wxDateTime::Now().Format("%Y-%m-%d_%H-%M-%S") + ".log";

        wxFFileOutputStream* fstream = new wxFFileOutputStream(logPath);
        if (fstream->IsOk())
        {
            new wxLogChain(new wxLogStream(fstream));
            wxLogMessage("Log file: %s", logPath);
        }

        wxLogMessage("ZipFX started (debug build)");
#endif

        m_locale.Init(wxLANGUAGE_DEFAULT);
        m_locale.AddCatalogLookupPathPrefix("./locale");
        m_locale.AddCatalogLookupPathPrefix(
            wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + "/locale");
        m_locale.AddCatalog("ZipFX");

        auto frame = new MainFrame();
        frame->Show(true);
        return true;
    }

    int OnExit() override
    {
        wxLogDebug("ZipFX shutting down");
        return wxApp::OnExit();
    }

private:
    wxLocale m_locale;
};

wxIMPLEMENT_APP(ZipFXApp);
