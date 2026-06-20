#ifndef ZIPFX_MAINFRAME_H
#define ZIPFX_MAINFRAME_H

#include <wx/wx.h>

#include "FileListPanel.h"
#include "icons.h"

#include <memory>
#include <string>
#include <vector>

class ArchiveEngine;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame() override;

private:
    // Archive actions
    void OnNewArchive();
    void OnOpenArchive();
    void OnCloseArchive();
    void OnAbout();

    void OnToolAdd();
    void OnToolExtract();
    void OnToolTest();
    void OnToolDelete();
    void OnToolView();

    void RefreshFileList();
    void DoExtract(const std::string& destPath);

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
    };
};

#endif
