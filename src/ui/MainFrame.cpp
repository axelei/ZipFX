#include "MainFrame.h"
#include "CreateArchiveDialog.h"

#include <wx/log.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/dirdlg.h>
#include <wx/progdlg.h>
#include <wx/checkbox.h>
#include <wx/dnd.h>
#include <wx/stdpaths.h>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"

// ── Drop target for receiving files from the OS ────────────────────────
class ZipFXDropTarget : public wxFileDropTarget
{
public:
    ZipFXDropTarget(MainFrame* frame) : m_frame(frame) {}

    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override
    {
        return m_frame->OnDropFiles(filenames);
    }

private:
    MainFrame* m_frame;
};

// ── Constructor ────────────────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, _("ZipFX"),
              wxDefaultPosition, wxSize(960, 640))
{
    m_icons = CreatePlaceholderIcons();

    // ── Menu bar ────────────────────────────────────────────────────
    auto menuBar  = new wxMenuBar();
    auto fileMenu = new wxMenu();
    fileMenu->Append(ID_NewArchive, _("&New Archive...\tCtrl+N"));
    fileMenu->Append(wxID_OPEN,     _("&Open Archive...\tCtrl+O"));
    fileMenu->Append(wxID_CLOSE,    _("&Close Archive\tCtrl+C"));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_SAVEAS,   _("Save Archive &As...\tCtrl+S"));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT,     _("E&xit\tAlt+F4"));
    menuBar->Append(fileMenu, _("&File"));

    auto cmdMenu = new wxMenu();
    cmdMenu->Append(ID_Add,     _("&Add Files to Archive...\tAlt+A"));
    cmdMenu->Append(ID_Extract, _("E&xtract Files...\tAlt+E"));
    cmdMenu->Append(ID_Test,    _("&Test Archive\tAlt+T"));
    cmdMenu->Append(ID_View,    _("&View File\tAlt+V"));
    cmdMenu->Append(ID_Delete,  _("&Delete Files\tDel"));
    cmdMenu->AppendSeparator();
    cmdMenu->Append(ID_Find,    _("&Find Files...\tF3"));
    cmdMenu->Append(ID_Wizard,  _("&Wizard...\tCtrl+W"));
    cmdMenu->Append(ID_Info,    _("&Information...\tCtrl+I"));
    menuBar->Append(cmdMenu, _("&Commands"));

    auto toolsMenu = new wxMenu();
    toolsMenu->Append(wxID_ANY,         _("&Repair Archive..."));
    toolsMenu->Append(wxID_ANY,         _("&Convert Archive..."));
    toolsMenu->Append(wxID_ANY,         _("&Benchmark..."));
    toolsMenu->AppendSeparator();
    toolsMenu->Append(wxID_PREFERENCES, _("&Settings..."));
    menuBar->Append(toolsMenu, _("&Tools"));

    auto favMenu = new wxMenu();
    favMenu->Append(wxID_ANY, _("&Add to Favorites"));
    favMenu->Append(wxID_ANY, _("&Organize Favorites..."));
    menuBar->Append(favMenu, _("&Favorites"));

    auto optsMenu = new wxMenu();
    optsMenu->AppendCheckItem(ID_ToggleFlatMode, _("&Flat File List"));
    optsMenu->AppendCheckItem(wxID_ANY,         _("&Toolbar"));
    optsMenu->AppendCheckItem(wxID_ANY,         _("&Status Bar"));
    menuBar->Append(optsMenu, _("&Options"));

    auto helpMenu = new wxMenu();
    helpMenu->Append(wxID_ABOUT, _("&About ZipFX"));
    menuBar->Append(helpMenu, _("&Help"));
    SetMenuBar(menuBar);

    // ── Toolbar ─────────────────────────────────────────────────────
    auto tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT);
    tb->SetToolBitmapSize(wxSize(20, 20));

    tb->AddTool(ID_Add,     _("Add"),        m_icons.add);
    tb->AddTool(ID_Extract, _("Extract To"), m_icons.extract);
    tb->AddTool(ID_Test,    _("Test"),       m_icons.test);
    tb->AddTool(ID_View,    _("View"),       m_icons.view);
    tb->AddSeparator();
    tb->AddTool(ID_Delete,  _("Delete"),     m_icons.del);
    tb->AddTool(ID_Find,    _("Find"),       m_icons.find);
    tb->AddSeparator();
    tb->AddTool(ID_Wizard,  _("Wizard"),     m_icons.wizard);
    tb->AddTool(ID_Info,    _("Info"),       m_icons.info);

    tb->Realize();

    // ── Address bar ─────────────────────────────────────────────────
    auto addrPanel = new wxPanel(this);
    auto addrSizer = new wxBoxSizer(wxHORIZONTAL);

    auto addrLabel = new wxStaticText(addrPanel, wxID_ANY, _("Address:"));
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
    SetStatusText(_("No archive open"), 0);
    SetStatusText("", 1);
    SetStatusText(_("Ready"), 2);

    // ── Layout ──────────────────────────────────────────────────────
    auto frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(addrPanel,   0, wxEXPAND | wxTOP, 2);
    frameSizer->Add(m_fileList,  1, wxEXPAND | wxALL,  2);
    SetSizer(frameSizer);

    // ── Event bindings ──────────────────────────────────────────────
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnNewArchive(); },  ID_NewArchive);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnOpenArchive(); }, wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnCloseArchive(); }, wxID_CLOSE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); },     wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnAbout(); },       wxID_ABOUT);

    auto bindCmd = [this](int id, void (MainFrame::*fn)())
    {
        Bind(wxEVT_MENU, [this, fn](wxCommandEvent&) { (this->*fn)(); }, id);
        Bind(wxEVT_TOOL, [this, fn](wxCommandEvent&) { (this->*fn)(); }, id);
    };

    bindCmd(ID_Add,     &MainFrame::OnToolAdd);
    bindCmd(ID_Extract, &MainFrame::OnToolExtract);
    bindCmd(ID_Test,    &MainFrame::OnToolTest);
    bindCmd(ID_View,    &MainFrame::OnToolView);
    bindCmd(ID_Delete,  &MainFrame::OnToolDelete);

    // Flat mode toggle
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        bool flat = !m_fileList->IsFlatMode();
        m_fileList->SetFlatMode(flat);
    }, ID_ToggleFlatMode);

    // "Up" button in address bar
    upBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        m_fileList->NavigateUp();
        m_addrBox->SetValue(m_fileList->GetCurrentDir());
    });

    // Sync address bar when user double-clicks a directory in the list
    m_fileList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&)
    {
        m_addrBox->SetValue(m_fileList->GetCurrentDir());
    });

    // ── Context menu (right-click) ──────────────────────────────
    m_fileList->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,
        &MainFrame::OnContextMenu, this);

    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        wxString entryPath = m_fileList->GetSelectedEntryPath();
        if (!entryPath.empty())
            m_engine->ReadFile(entryPath.ToStdString());
    }, ID_CtxView);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { DoExtractSelected(); }, ID_CtxExtract);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnToolDelete(); },  ID_CtxDelete);

    // ── Drag source (extract by dragging to explorer) ───────────
    m_fileList->Bind(wxEVT_LIST_BEGIN_DRAG, &MainFrame::OnBeginDrag, this);

    // ── Drop target (add files by dropping onto window) ─────────
    SetDropTarget(new ZipFXDropTarget(this));
}

MainFrame::~MainFrame()
{
    // Children are destroyed before this runs — do nothing here.
}

void MainFrame::OnContextMenu(wxListEvent& event)
{
    if (!m_engine)
    {
        return;
    }

    wxMenu menu;
    menu.Append(ID_CtxExtract, _("Extract..."));
    if (!m_fileList->IsFlatMode())
    {
        menu.Append(ID_CtxView, _("Open"));
    }
    if (m_engine->SupportsCreation())
    {
        menu.AppendSeparator();
        menu.Append(ID_CtxDelete, _("Delete"));
    }

    PopupMenu(&menu);
}

bool MainFrame::OnDropFiles(const wxArrayString& filenames)
{
    if (!m_engine || !m_engine->SupportsCreation())
    {
        wxMessageBox(_("No archive open or format does not support adding files."),
                     _("Error"), wxOK | wxICON_WARNING);
        return false;
    }

    wxString prefix = m_fileList->GetCurrentDir();
    if (!prefix.empty()) prefix += "/";

    for (const auto& path : filenames)
    {
        wxFileName fn(path);
        wxString archivePath = prefix + fn.GetFullName();
        m_engine->AddFile(path.ToStdString(), archivePath.ToStdString());
    }

    if (!m_engine->Save())
    {
        wxLogError("Failed to save archive after drop");
        wxMessageBox(_("Could not save the archive."),
                     _("Error"), wxOK | wxICON_ERROR);
    }

    RefreshFileList();
    return true;
}

void MainFrame::OnBeginDrag(wxListEvent& event)
{
    if (!m_engine)
    {
        wxLogDebug("Drag: no engine");
        return;
    }

    wxLogDebug("Drag: engine=%s isOpen=%d isWriter=%d",
               m_engine->FormatName().data(),
               m_engine->IsOpen(), false);

    auto sel = m_fileList->GetSelectedEntryPaths();
    if (sel.empty())
    {
        return;
    }

    // Unique temp folder for this drag operation
    wxString tmpRoot = wxStandardPaths::Get().GetTempDir()
                     + "/ZipFX_Drag/"
                     + wxString::Format("%u", (unsigned)time(nullptr))
                     + "/";

    wxFileName::Mkdir(tmpRoot, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxFileDataObject data;
    int okCount = 0;

    auto allEntries = m_engine->ListContents();

    // Expand selection: directories → all files inside them
    std::vector<wxString> filePaths;
    for (const auto& entryPath : sel)
    {
        bool isDir = false;
        std::string entryStr = entryPath.ToStdString();

        // Try matching with and without trailing slash
        for (const auto& e : allEntries)
        {
            if (e.path == entryStr ||
                e.path == entryStr + "/")
            {
                isDir = e.isDirectory;
                break;
            }
        }

        if (isDir)
        {
            std::string prefix = entryStr + "/";
            for (const auto& e : allEntries)
            {
                if (!e.isDirectory &&
                    e.path.compare(0, prefix.size(), prefix) == 0)
                {
                    filePaths.push_back(wxString::FromUTF8(e.path.c_str()));
                }
            }
        }
        else
        {
            filePaths.push_back(entryPath);
        }
    }

    for (const auto& entryPath : filePaths)
    {
        wxFileName tmpFile(tmpRoot + entryPath);
        tmpFile.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_TILDE | wxPATH_NORM_ABSOLUTE);

        wxFileName::Mkdir(tmpFile.GetPath(),
                          wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        if (m_engine->Extract(entryPath.ToStdString(), tmpFile.GetFullPath().ToStdString()))
        {
            data.AddFile(tmpFile.GetFullPath());
            okCount++;
        }
        else
        {
            wxLogWarning("Drag: failed to extract %s  →  %s",
                         entryPath, tmpFile.GetFullPath());
        }
    }

    if (okCount > 0)
    {
        wxLogDebug("Starting drag with %d temp files", okCount);
        wxDropSource source(data, this);
        source.DoDragDrop(wxDrag_CopyOnly);
    }
    else
    {
        wxLogWarning("No files extracted for drag");
        wxMessageBox(_("Could not extract the selected files. "
                       "Only archive entries are supported for drag."),
                     _("Drag Failed"), wxOK | wxICON_INFORMATION);
    }
}

// ── New Archive ────────────────────────────────────────────────────────
void MainFrame::OnNewArchive()
{
    CreateArchiveDialog dlg(this);
    if (dlg.ShowModal() != wxID_OK)
    {
        return;
    }

    auto result = dlg.GetResult();
    OnCloseArchive();

    auto engine = ArchiveEngineFactory::CreateForFormat(
        result.format.ToStdString());

    if (!engine)
    {
        wxLogError("No engine for format: %s", result.format);
        wxMessageBox(_("This format is not supported."),
                     _("Error"), wxOK | wxICON_ERROR);
        return;
    }

    if (!engine->SupportsCreation())
    {
        wxMessageBox(_("This format does not support archive creation."),
                     _("Error"), wxOK | wxICON_ERROR);
        return;
    }

    wxLogDebug("Creating %s archive: %s  (level %d)",
               result.format, result.path, result.compressionLevel);

    if (!engine->Create(result.path.ToStdString()))
    {
        wxLogError("Failed to create archive: %s", result.path);
        wxMessageBox(_("Could not create the archive."),
                     _("Error"), wxOK | wxICON_ERROR);
        return;
    }

    m_engine = std::move(engine);
    m_currentPath = result.path.ToStdString();
    m_addrBox->SetValue(m_currentPath);

    wxLogMessage("Archive created: %s", m_currentPath);
    RefreshFileList();
}

// ── Open Archive ───────────────────────────────────────────────────────
void MainFrame::OnOpenArchive()
{
    wxString filter =
        "Supported Archives (*.zip;*.7z;*.rar;*.tar;*.tgz;*.tar.gz)"
        "|*.zip;*.7z;*.rar;*.tar;*.tgz;*.tar.gz"
        "|ZIP Files (*.zip)|*.zip"
        "|7z Files (*.7z)|*.7z"
        "|RAR Files (*.rar)|*.rar"
        "|TAR Files (*.tar;*.tgz;*.tar.gz)|*.tar;*.tgz;*.tar.gz"
        "|All Files (*.*)|*.*";

    wxFileDialog dlg(this, _("Open Archive"), "", "", filter,
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dlg.ShowModal() != wxID_OK)
    {
        return;
    }

    OnCloseArchive();

    auto path = dlg.GetPath().ToStdString();
    wxLogDebug("Opening archive: %s", path);

    auto engine = ArchiveEngineFactory::CreateForFile(path);

    if (!engine || !engine->Open(path))
    {
        wxLogError("Failed to open archive: %s", path);
        wxMessageBox(_("Could not open the archive. It may be "
                       "corrupted or in an unsupported format."),
                     _("Open Failed"), wxOK | wxICON_ERROR);
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
        wxLogDebug("Closing archive: %s", m_currentPath);
        m_engine->Close();
        m_engine.reset();
    }

    m_currentPath.clear();
    m_fileList->Clear();
    m_addrBox->SetValue("");

    SetStatusText(_("No archive open"), 0);
    SetStatusText("", 1);
}

void MainFrame::OnAbout()
{
    wxMessageBox(
        _("ZipFX v1.0\n\n"
          "A cross-platform archive manager.\n"
          "Supported formats: ZIP, 7z, RAR, TAR.GZ"),
        _("About ZipFX"),
        wxOK | wxICON_INFORMATION);
}

// ── Add Files ──────────────────────────────────────────────────────────
void MainFrame::OnToolAdd()
{
    if (!m_engine)
    {
        wxMessageBox(_("No archive is open."), _("Error"),
                     wxOK | wxICON_WARNING);
        return;
    }

    if (!m_engine->SupportsCreation())
    {
        wxMessageBox(_("This archive format does not support adding files."),
                     _("Error"), wxOK | wxICON_WARNING);
        return;
    }

    wxFileDialog dlg(this, _("Add Files"), "", "", "*.*",
        wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dlg.ShowModal() != wxID_OK)
    {
        return;
    }

    wxArrayString paths;
    dlg.GetPaths(paths);

    wxString prefix = m_fileList->GetCurrentDir();
    if (!prefix.empty())
    {
        prefix += "/";
    }

    wxProgressDialog progress(_("Adding Files"),
        _("Preparing..."), static_cast<int>(paths.size()) + 1, this,
        wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_APP_MODAL);

    for (int i = 0; i < static_cast<int>(paths.size()); ++i)
    {
        wxFileName fn(paths[i]);
        wxString archivePath = prefix + fn.GetFullName();

        wxString msg = wxString::Format(_("Adding: %s"), fn.GetFullName());
        if (!progress.Update(i, msg))
        {
            break;
        }

        if (!m_engine->AddFile(paths[i].ToStdString(), archivePath.ToStdString()))
        {
            wxLogWarning("Failed to add file: %s", paths[i]);
        }
    }

    progress.Update(static_cast<int>(paths.size()), _("Saving archive..."));

    if (!m_engine->Save())
    {
        wxLogError("Failed to save archive after adding files");
        wxMessageBox(_("Could not save the archive after adding files."),
                     _("Error"), wxOK | wxICON_ERROR);
    }

    wxLogMessage("Files added to archive");
    RefreshFileList();
}

// ── Overwrite confirmation dialog ──────────────────────────────────────
enum class OverwriteAction { Yes, YesToAll, No, NoToAll, Cancel };

static OverwriteAction ConfirmOverwrite(wxWindow* parent,
    const wxString& filename, bool& applyToAll)
{
    if (applyToAll)
    {
        return OverwriteAction::YesToAll;
    }

    wxDialog dlg(parent, wxID_ANY, _("Confirm Overwrite"),
                 wxDefaultPosition, wxSize(420, 170));
    auto vs = new wxBoxSizer(wxVERTICAL);

    vs->Add(new wxStaticText(&dlg, wxID_ANY,
        wxString::Format(_("File \"%s\" already exists.\nOverwrite?"), filename)),
        0, wxALL, 12);

    auto cb = new wxCheckBox(&dlg, wxID_ANY, _("Apply to all files"));
    vs->Add(cb, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto hs = new wxBoxSizer(wxHORIZONTAL);
    auto btnYes   = new wxButton(&dlg, wxID_YES, _("Overwrite"));
    auto btnNo    = new wxButton(&dlg, wxID_NO,  _("Skip"));
    auto btnCancel = new wxButton(&dlg, wxID_CANCEL, _("Cancel"));

    hs->Add(btnYes,    0, wxRIGHT, 6);
    hs->Add(btnNo,     0, wxRIGHT, 6);
    hs->Add(btnCancel, 0);
    vs->Add(hs, 0, wxALIGN_CENTER | wxBOTTOM, 10);

    dlg.SetSizer(vs);

    btnYes->Bind(wxEVT_BUTTON, [&](wxCommandEvent&)
    {
        applyToAll = cb->GetValue();
        dlg.EndModal(wxID_YES);
    });
    btnNo->Bind(wxEVT_BUTTON, [&](wxCommandEvent&)
    {
        applyToAll = cb->GetValue();
        dlg.EndModal(wxID_NO);
    });
    btnCancel->Bind(wxEVT_BUTTON, [&](wxCommandEvent&)
    {
        dlg.EndModal(wxID_CANCEL);
    });

    int ret = dlg.ShowModal();
    if (ret == wxID_YES)    return OverwriteAction::Yes;
    if (ret == wxID_NO)     return OverwriteAction::No;
    return OverwriteAction::Cancel;
}

// ── Extract ────────────────────────────────────────────────────────────
void MainFrame::OnToolExtract()
{
    if (!m_engine)
    {
        wxMessageBox(_("No archive is open."), _("Error"),
                     wxOK | wxICON_WARNING);
        return;
    }

    wxDirDialog dlg(this, _("Select destination folder"), "",
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

    if (dlg.ShowModal() != wxID_OK)
    {
        return;
    }

    DoExtract(dlg.GetPath().ToStdString());
}

void MainFrame::DoExtract(const std::string& destPath)
{
    wxLogDebug("Extracting to: %s", destPath);

    auto entries = m_engine->ListContents();
    if (entries.empty())
    {
        wxMessageBox(_("The archive is empty."), _("Info"),
                     wxOK | wxICON_INFORMATION);
        return;
    }

    wxProgressDialog progress(_("Extracting"),
        _("Preparing..."),
        static_cast<int>(entries.size()),
        this,
        wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_APP_MODAL);

    bool applyToAll = false;
    int extracted = 0;
    int skipped   = 0;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];

        wxString msg = wxString::Format(_("Extracting: %s"),
            wxString::FromUTF8(entry.name.c_str()));

        if (!progress.Update(static_cast<int>(i), msg))
        {
            wxLogDebug("Extraction cancelled by user");
            break;
        }

        wxString destFile = wxString::FromUTF8(
            (destPath + "/" + entry.name).c_str());

        // Overwrite check
        if (wxFileExists(destFile) || wxDirExists(destFile))
        {
            OverwriteAction action = ConfirmOverwrite(
                this, wxString::FromUTF8(entry.name.c_str()), applyToAll);

            if (action == OverwriteAction::Cancel)
            {
                wxLogDebug("Extraction cancelled by user");
                break;
            }
            if (action == OverwriteAction::No)
            {
                skipped++;
                continue;
            }
            // Yes / YesToAll falls through to extraction
        }

        if (entry.isDirectory)
        {
            wxFileName::Mkdir(destFile, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            continue;
        }

        wxFileName::Mkdir(wxFileName(destFile).GetPath(),
                          wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        if (!m_engine->Extract(entry.name, destFile.ToStdString()))
        {
            wxLogWarning("Failed to extract: %s", entry.name);
        }
        else
        {
            extracted++;
        }
    }

    progress.Update(static_cast<int>(entries.size()));

    wxLogMessage("Extraction complete: %d extracted, %d skipped", extracted, skipped);
    SetStatusText(
        wxString::Format(_("Extraction complete (%d files)"), extracted), 2);
}

void MainFrame::DoExtractSelected()
{
    if (!m_engine) return;

    auto sel = m_fileList->GetSelectedEntryPaths();
    if (sel.empty())
    {
        wxMessageBox(_("Please select at least one file."), _("Info"),
                     wxOK | wxICON_INFORMATION);
        return;
    }

    wxDirDialog dirDlg(this, _("Extract selected to"), "",
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dirDlg.ShowModal() != wxID_OK) return;

    wxString destRoot = dirDlg.GetPath();
    wxLogDebug("Extracting %zu selected entries to %s",
               sel.size(), destRoot);

    wxProgressDialog progress(_("Extracting"),
        _("Preparing..."),
        static_cast<int>(sel.size()), this,
        wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_APP_MODAL);

    int extracted = 0;

    auto allEntries = m_engine->ListContents();

    // Expand selection: directories → all files inside them
    std::vector<wxString> filePaths;
    for (const auto& entryPath : sel)
    {
        bool isDir = false;
        std::string entryStr = entryPath.ToStdString();

        for (const auto& e : allEntries)
        {
            if (e.path == entryStr ||
                e.path == entryStr + "/")
            {
                isDir = e.isDirectory;
                break;
            }
        }

        if (isDir)
        {
            std::string prefix = entryStr + "/";
            for (const auto& e : allEntries)
            {
                if (!e.isDirectory &&
                    e.path.compare(0, prefix.size(), prefix) == 0)
                {
                    filePaths.push_back(wxString::FromUTF8(e.path.c_str()));
                }
            }
        }
        else
        {
            filePaths.push_back(entryPath);
        }
    }

    for (size_t i = 0; i < filePaths.size(); ++i)
    {
        wxString msg = wxString::Format(_("Extracting: %s"), filePaths[i]);
        if (!progress.Update(static_cast<int>(i), msg)) break;

        wxString destFile = destRoot + "/" + filePaths[i].AfterLast('/');

        wxFileName::Mkdir(wxFileName(destFile).GetPath(),
                          wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

        if (m_engine->Extract(filePaths[i].ToStdString(), destFile.ToStdString()))
        {
            extracted++;
        }
        else
        {
            wxLogWarning("Failed to extract: %s", filePaths[i]);
        }
    }

    progress.Update(static_cast<int>(sel.size()));
    wxLogMessage("Extracted %d selected files", extracted);
    SetStatusText(wxString::Format(_("Extracted %d files"), extracted), 2);
}

// ── Test ───────────────────────────────────────────────────────────────
void MainFrame::OnToolTest()
{
    if (!m_engine)
    {
        wxMessageBox(_("No archive is open."), _("Error"),
                     wxOK | wxICON_WARNING);
        return;
    }

    wxLogDebug("Testing archive integrity");

    if (m_engine->TestIntegrity())
    {
        wxMessageBox(_("Archive integrity check passed."),
                     _("Test Result"), wxOK | wxICON_INFORMATION);
    }
    else
    {
        wxMessageBox(_("Archive integrity check FAILED."),
                     _("Test Result"), wxOK | wxICON_ERROR);
    }
}

// ── View & Delete ──────────────────────────────────────────────────────
void MainFrame::OnToolView()
{
    if (!m_engine)
    {
        wxMessageBox(_("No archive is open."), _("Error"),
                     wxOK | wxICON_WARNING);
        return;
    }

    if (!m_fileList->IsFlatMode() && m_fileList->IsSelectedDirectory())
    {
        m_fileList->NavigateInto(
            m_fileList->GetItemText(m_fileList->GetSelectedIndex(), 0));
        return;
    }

    wxString entryPath = m_fileList->GetSelectedEntryPath();
    if (entryPath.empty())
    {
        wxMessageBox(_("Please select a file first."), _("Info"),
                     wxOK | wxICON_INFORMATION);
        return;
    }

    wxLogDebug("Viewing file: %s", entryPath);
    auto data = m_engine->ReadFile(entryPath.ToStdString());

    if (data.empty())
    {
        wxMessageBox(_("Could not read the file."),
                     _("Error"), wxOK | wxICON_ERROR);
        return;
    }

    wxString msg = wxString::Format(
        _("File: %s\nSize: %zu bytes"), entryPath, data.size());
    wxMessageBox(msg, _("File Information"), wxOK | wxICON_INFORMATION);
}

void MainFrame::OnToolDelete()
{
    if (!m_engine)
    {
        wxMessageBox(_("No archive is open."), _("Error"),
                     wxOK | wxICON_WARNING);
        return;
    }

    if (!m_engine->SupportsCreation())
    {
        wxMessageBox(_("This archive format does not support deletion."),
                     _("Error"), wxOK | wxICON_WARNING);
        return;
    }

    if (m_fileList->IsSelectedDirectory())
    {
        wxMessageBox(_("Cannot delete a folder."), _("Info"),
                     wxOK | wxICON_INFORMATION);
        return;
    }

    wxString entryPath = m_fileList->GetSelectedEntryPath();
    if (entryPath.empty())
    {
        wxMessageBox(_("Please select a file first."), _("Info"),
                     wxOK | wxICON_INFORMATION);
        return;
    }

    if (wxMessageBox(
            wxString::Format(_("Delete \"%s\" from archive?"), entryPath),
            _("Confirm Delete"),
            wxYES_NO | wxICON_QUESTION) != wxYES)
    {
        return;
    }

    wxLogDebug("Deleting entry: %s", entryPath);

    if (!m_engine->RemoveEntry(entryPath.ToStdString()) || !m_engine->Save())
    {
        wxLogError("Failed to delete entry: %s", entryPath);
        wxMessageBox(_("Could not delete the file."),
                     _("Error"), wxOK | wxICON_ERROR);
        return;
    }

    wxLogMessage("Deleted entry: %s", entryPath);
    RefreshFileList();
}

// ── Refresh ────────────────────────────────────────────────────────────
void MainFrame::RefreshFileList()
{
    if (!m_engine)
    {
        return;
    }

    auto entries = m_engine->ListContents();
    wxLogDebug("Listing %zu entries", entries.size());
    m_fileList->SetEntries(entries);

    SetStatusText(
        wxString::Format(_("%zu files"), entries.size()), 0);
    SetStatusText(m_currentPath, 1);
}
