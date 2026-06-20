#include "MainFrame.h"

#include <wx/aboutdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"

// ── Constructor ────────────────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "ZipFX",
              wxDefaultPosition, wxSize(960, 640))
{
    m_icons = CreatePlaceholderIcons();

    // ── Menu bar ────────────────────────────────────────────────────
    auto menuBar  = new wxMenuBar();
    auto fileMenu = new wxMenu();
    fileMenu->Append(wxID_OPEN,        "&Open Archive...\tCtrl+O");
    fileMenu->Append(wxID_CLOSE,       "&Close Archive\tCtrl+C");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_SAVEAS,      "Save Archive &As...\tCtrl+S");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT,        "E&xit\tAlt+F4");
    menuBar->Append(fileMenu, "&File");

    auto cmdMenu = new wxMenu();
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

    auto toolsMenu = new wxMenu();
    toolsMenu->Append(wxID_ANY, "&Repair Archive...");
    toolsMenu->Append(wxID_ANY, "&Convert Archive...");
    toolsMenu->Append(wxID_ANY, "&Benchmark...");
    toolsMenu->AppendSeparator();
    toolsMenu->Append(wxID_PREFERENCES, "&Settings...");
    menuBar->Append(toolsMenu, "&Tools");

    auto favMenu = new wxMenu();
    favMenu->Append(wxID_ANY, "&Add to Favorites");
    favMenu->Append(wxID_ANY, "&Organize Favorites...");
    menuBar->Append(favMenu, "&Favorites");

    auto optsMenu = new wxMenu();
    optsMenu->AppendCheckItem(wxID_ANY, "&Toolbar");
    optsMenu->AppendCheckItem(wxID_ANY, "&Status Bar");
    menuBar->Append(optsMenu, "&Options");

    auto helpMenu = new wxMenu();
    helpMenu->Append(wxID_ABOUT, "&About ZipFX");
    menuBar->Append(helpMenu, "&Help");
    SetMenuBar(menuBar);

    // ── Toolbar ─────────────────────────────────────────────────────
    auto tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT);
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

    // ── Address bar ─────────────────────────────────────────────────
    auto addrPanel = new wxPanel(this);
    auto addrSizer = new wxBoxSizer(wxHORIZONTAL);

    auto addrLabel = new wxStaticText(addrPanel, wxID_ANY, "Address:");
    m_addrBox = new wxComboBox(addrPanel, wxID_ANY,
        "", wxDefaultPosition, wxSize(400, -1), 0, nullptr,
        wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
    auto upBtn = new wxButton(addrPanel, wxID_UP, "..",
        wxDefaultPosition, wxSize(28, -1));

    addrSizer->Add(addrLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    addrSizer->Add(m_addrBox, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    addrSizer->Add(upBtn,     0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    addrPanel->SetSizer(addrSizer);

    // ── File list ───────────────────────────────────────────────────
    m_fileList = new FileListPanel(this);

    // ── Status bar ──────────────────────────────────────────────────
    CreateStatusBar(3);
    SetStatusText("No archive open", 0);
    SetStatusText("", 1);
    SetStatusText("Ready", 2);

    // ── Layout ──────────────────────────────────────────────────────
    auto frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(addrPanel,   0, wxEXPAND | wxTOP, 2);
    frameSizer->Add(m_fileList,  1, wxEXPAND | wxALL,  2);
    SetSizer(frameSizer);

    // ── Event bindings ──────────────────────────────────────────────
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnOpenArchive(); }, wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnCloseArchive(); }, wxID_CLOSE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnAbout(); }, wxID_ABOUT);
}

MainFrame::~MainFrame()
{
    OnCloseArchive();
}

// ── Actions ────────────────────────────────────────────────────────────
void MainFrame::OnOpenArchive()
{
    wxFileDialog dlg(this, "Open Archive", "", "",
        "Supported Archives (*.zip;*.7z;*.rar;*.tar;*.tgz;*.tar.gz)"
        "|*.zip;*.7z;*.rar;*.tar;*.tgz;*.tar.gz"
        "|ZIP Files (*.zip)|*.zip"
        "|7z Files (*.7z)|*.7z"
        "|RAR Files (*.rar)|*.rar"
        "|TAR Files (*.tar;*.tgz;*.tar.gz)|*.tar;*.tgz;*.tar.gz"
        "|All Files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dlg.ShowModal() != wxID_OK)
    {
        return;
    }

    OnCloseArchive();

    auto path = dlg.GetPath().ToStdString();
    auto engine = ArchiveEngineFactory::CreateForFile(path);

    if (!engine || !engine->Open(path))
    {
        wxMessageBox("Could not open the archive. It may be "
                     "corrupted or in an unsupported format.",
                     "Open Failed", wxOK | wxICON_ERROR);
        return;
    }

    m_engine = std::move(engine);
    m_currentPath = path;
    m_addrBox->SetValue(path);

    RefreshFileList();
}

void MainFrame::OnCloseArchive()
{
    if (m_engine)
    {
        m_engine->Close();
        m_engine.reset();
    }

    m_currentPath.clear();
    m_fileList->Clear();
    m_addrBox->SetValue("");

    SetStatusText("No archive open", 0);
    SetStatusText("", 1);
}

void MainFrame::OnAbout()
{
    wxMessageBox(
        "ZipFX v1.0\n\n"
        "A cross-platform archive manager.\n"
        "Supported formats: ZIP, 7z, RAR, TAR.GZ",
        "About ZipFX",
        wxOK | wxICON_INFORMATION);
}

void MainFrame::RefreshFileList()
{
    if (!m_engine)
    {
        return;
    }

    auto entries = m_engine->ListContents();
    m_fileList->SetEntries(entries);

    SetStatusText(
        wxString::Format("%zu files", entries.size()), 0);
    SetStatusText(m_currentPath, 1);
}
