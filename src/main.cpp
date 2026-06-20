#include <wx/wx.h>
#include <wx/log.h>

#include "ui/MainFrame.h"

class ZipFXApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxInitAllImageHandlers();

#ifdef _DEBUG
        // Forward logs to stderr in debug builds (error dialogs still appear)
        new wxLogChain(new wxLogStream(&std::clog));
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
