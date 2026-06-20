#include "ui/MainFrame.h"

class ZipFXApp : public wxApp
{
public:
    bool OnInit() override
    {
        auto frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(ZipFXApp);
