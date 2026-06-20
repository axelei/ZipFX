#ifndef ZIPFX_MAINFRAME_H
#define ZIPFX_MAINFRAME_H

#include <wx/wx.h>

#include "FileListPanel.h"
#include "icons.h"

#include <memory>

class ArchiveEngine;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame() override;

private:
    void OnOpenArchive();
    void OnCloseArchive();
    void OnAbout();

    void OnToolAdd();
    void OnToolExtract();

    void RefreshFileList();

    ZipFXIcons m_icons;
    FileListPanel* m_fileList = nullptr;
    wxComboBox* m_addrBox = nullptr;
    wxStatusBar* m_statusBar = nullptr;

    std::unique_ptr<ArchiveEngine> m_engine;
    std::string m_currentPath;
};

#endif
