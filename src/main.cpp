#include <wx/wx.h>

#include "ui/MainFrame.h"

class ZipFXApp : public wxApp
{
public:
    bool OnInit() override
    {
        wxInitAllImageHandlers();

        m_locale.Init(wxLANGUAGE_DEFAULT);
        m_locale.AddCatalogLookupPathPrefix("./locale");
        m_locale.AddCatalogLookupPathPrefix(
            wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + "/locale");
        m_locale.AddCatalog("ZipFX");

        auto frame = new MainFrame();
        frame->Show(true);
        return true;
    }

private:
    wxLocale m_locale;
};

wxIMPLEMENT_APP(ZipFXApp);
