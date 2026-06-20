#include <wx/wx.h>
#include <wx/listctrl.h>

#include "icons.h"

// -----------------------------------------------------------------------
// ZipFXFrame  –  main window mimicking WinRAR's layout
// -----------------------------------------------------------------------
class ZipFXFrame : public wxFrame
{
public:
    ZipFXFrame()
        : wxFrame(nullptr, wxID_ANY, "ZipFX",
                   wxDefaultPosition, wxSize(960, 640))
    {
        // ── Icons ──────────────────────────────────────────────────
        m_icons = CreatePlaceholderIcons();

        // ── Menu bar ──────────────────────────────────────────────
        wxMenuBar* menuBar = new wxMenuBar();

        wxMenu* fileMenu = new wxMenu();
        fileMenu->Append(wxID_OPEN,        "&Open Archive...\tCtrl+O");
        fileMenu->Append(wxID_ANY,         "&Close Archive\tCtrl+C");
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_SAVEAS,      "Save Archive &As...\tCtrl+S");
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT,        "E&xit\tAlt+F4");
        menuBar->Append(fileMenu, "&File");

        wxMenu* cmdMenu = new wxMenu();
        cmdMenu->Append(wxID_ANY, "&Add Files to Archive...\tAlt+A");
        cmdMenu->Append(wxID_ANY, "E&xtract Files...\tAlt+E");
        cmdMenu->Append(wxID_ANY, "&Test Archive\tAlt+T");
        cmdMenu->Append(wxID_ANY, "&View File\tAlt+V");
        cmdMenu->Append(wxID_ANY, "&Delete Files\tDel");
        cmdMenu->AppendSeparator();
        cmdMenu->Append(wxID_ANY, "&Find Files...\tF3");
        cmdMenu->Append(wxID_ANY, "&Wizard...\tCtrl+W");
        cmdMenu->Append(wxID_ANY, "&Information...\tCtrl+I");
        menuBar->Append(cmdMenu, "&Commands");

        wxMenu* toolsMenu = new wxMenu();
        toolsMenu->Append(wxID_ANY, "&Repair Archive...");
        toolsMenu->Append(wxID_ANY, "&Convert Archive...");
        toolsMenu->Append(wxID_ANY, "&Benchmark...");
        toolsMenu->AppendSeparator();
        toolsMenu->Append(wxID_PREFERENCES, "&Settings...");
        menuBar->Append(toolsMenu, "&Tools");

        wxMenu* favMenu = new wxMenu();
        favMenu->Append(wxID_ANY, "&Add to Favorites");
        favMenu->Append(wxID_ANY, "&Organize Favorites...");
        menuBar->Append(favMenu, "&Favorites");

        wxMenu* optsMenu = new wxMenu();
        optsMenu->AppendCheckItem(wxID_ANY, "&Toolbar");
        optsMenu->AppendCheckItem(wxID_ANY, "&Status Bar");
        menuBar->Append(optsMenu, "&Options");

        wxMenu* helpMenu = new wxMenu();
        helpMenu->Append(wxID_ABOUT, "&About ZipFX");
        menuBar->Append(helpMenu, "&Help");

        SetMenuBar(menuBar);

        // ── Toolbar ───────────────────────────────────────────────
        wxToolBar* tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT);
        tb->SetToolBitmapSize(wxSize(20, 20));

        tb->AddTool(wxID_ANY, "Add",        m_icons.add);
        tb->AddTool(wxID_ANY, "Extract To", m_icons.extract);
        tb->AddTool(wxID_ANY, "Test",       m_icons.test);
        tb->AddTool(wxID_ANY, "View",       m_icons.view);
        tb->AddSeparator();
        tb->AddTool(wxID_ANY, "Delete",     m_icons.del);
        tb->AddTool(wxID_ANY, "Find",       m_icons.find);
        tb->AddSeparator();
        tb->AddTool(wxID_ANY, "Wizard",     m_icons.wizard);
        tb->AddTool(wxID_ANY, "Info",       m_icons.info);

        tb->Realize();

        // ── Address bar ───────────────────────────────────────────
        wxPanel* addrPanel = new wxPanel(this);
        wxBoxSizer* addrSizer = new wxBoxSizer(wxHORIZONTAL);

        wxStaticText* addrLabel = new wxStaticText(addrPanel, wxID_ANY,
            "Address:");
        wxComboBox* addrBox = new wxComboBox(addrPanel, wxID_ANY,
            "", wxDefaultPosition, wxSize(400, -1), 0, nullptr,
            wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
        wxButton* upBtn = new wxButton(addrPanel, wxID_UP, "..",
            wxDefaultPosition, wxSize(28, -1));

        addrSizer->Add(addrLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        addrSizer->Add(addrBox,   1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        addrSizer->Add(upBtn,     0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
        addrPanel->SetSizer(addrSizer);

        // ── File list ─────────────────────────────────────────────
        m_fileList = new wxListCtrl(this, wxID_ANY,
            wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);

        m_fileList->AppendColumn("Name",     wxLIST_FORMAT_LEFT,  240);
        m_fileList->AppendColumn("Size",     wxLIST_FORMAT_RIGHT,  90);
        m_fileList->AppendColumn("Packed",   wxLIST_FORMAT_RIGHT,  90);
        m_fileList->AppendColumn("Type",     wxLIST_FORMAT_LEFT,  140);
        m_fileList->AppendColumn("Modified", wxLIST_FORMAT_LEFT,  140);
        m_fileList->AppendColumn("CRC",      wxLIST_FORMAT_LEFT,   80);

        long idx = m_fileList->InsertItem(0, "document.pdf");
        m_fileList->SetItem(idx, 1, "1 250 240");
        m_fileList->SetItem(idx, 2, "1 124 900");
        m_fileList->SetItem(idx, 3, "PDF Document");
        m_fileList->SetItem(idx, 4, "2026-06-15 14:30");
        m_fileList->SetItem(idx, 5, "A1B2C3D4");

        idx = m_fileList->InsertItem(1, "photo.jpg");
        m_fileList->SetItem(idx, 1,   "4 800 000");
        m_fileList->SetItem(idx, 2,   "4 512 000");
        m_fileList->SetItem(idx, 3, "JPEG Image");
        m_fileList->SetItem(idx, 4, "2026-06-10 09:15");
        m_fileList->SetItem(idx, 5, "E5F67890");

        idx = m_fileList->InsertItem(2, "source_code.zip");
        m_fileList->SetItem(idx, 1,     "892 000");
        m_fileList->SetItem(idx, 2,     "120 000");
        m_fileList->SetItem(idx, 3, "ZIP Archive");
        m_fileList->SetItem(idx, 4, "2026-05-28 18:42");
        m_fileList->SetItem(idx, 5, "1A2B3C4D");

        // ── Layout ────────────────────────────────────────────────
        wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
        frameSizer->Add(addrPanel,  0, wxEXPAND | wxTOP, 2);
        frameSizer->Add(m_fileList, 1, wxEXPAND | wxALL,  2);
        SetSizer(frameSizer);

        // ── Status bar ────────────────────────────────────────────
        CreateStatusBar(3);
        SetStatusText("3 files",         0);
        SetStatusText("6 942 240 bytes", 1);
        SetStatusText("Ready",           2);

        // ── Event bindings ────────────────────────────────────────
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
        Bind(wxEVT_MENU, [](wxCommandEvent&)
        {
            wxMessageBox("ZipFX v1.0\n\nA cross-platform archive manager.",
                         "About ZipFX", wxOK | wxICON_INFORMATION);
        }, wxID_ABOUT);
    }

private:
    ZipFXIcons  m_icons;
    wxListCtrl* m_fileList = nullptr;
};

// -----------------------------------------------------------------------
// ZipFXApp
// -----------------------------------------------------------------------
class ZipFXApp : public wxApp
{
public:
    bool OnInit() override
    {
        auto frame = new ZipFXFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(ZipFXApp);
