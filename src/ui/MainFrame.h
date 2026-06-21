#ifndef ZIPFX_MAINFRAME_H
#define ZIPFX_MAINFRAME_H

#include <wx/wx.h>
#include <wx/progdlg.h>

#include "FileListPanel.h"
#include "icons.h"
#include "ExtractionWorker.h"

#include <memory>
#include <string>
#include <vector>

class ArchiveEngine;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame() override;
    bool OnDropFiles(const wxArrayString& filenames);

private:
    // Archive actions
    void OnNewArchive();
    void OnOpenArchive();
    void OnCloseArchive();
    void OnAbout();

    void OnToolAdd();
    void OnToolExtract();
    void OnToolTest();
    void OnToolView();
    void OnToolDelete();

    // Context menu / drag & drop
    void OnContextMenu(wxListEvent&);
    void OnBeginDrag(wxListEvent&);

    void RefreshFileList();
    void DoExtract(const std::string& destPath);
    void DoExtractSelected();

    // Background extraction
    void OnExtractProgress(wxThreadEvent&);
    void OnExtractDone(wxThreadEvent&);
    std::unique_ptr<ExtractionWorker> m_extractWorker;
    wxProgressDialog* m_progressDlg = nullptr;

    ZipFXIcons m_icons;
    FileListPanel* m_fileList = nullptr;
    wxComboBox* m_addrBox = nullptr;

    std::unique_ptr<ArchiveEngine> m_engine;
    std::string m_currentPath;

    // Toolbar command IDs
    enum
    {
        ID_Add = wxID_HIGHEST + 1,
        ID_Extract,
        ID_Test,
        ID_View,
        ID_Delete,
        ID_Find,
        ID_Wizard,
        ID_Info,
        ID_NewArchive,
        ID_ToggleFlatMode,
        ID_CtxExtract,
        ID_CtxView,
        ID_CtxDelete,
    };
};

#endif
