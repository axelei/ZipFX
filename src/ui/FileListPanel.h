#ifndef ZIPFX_FILE_LIST_PANEL_H
#define ZIPFX_FILE_LIST_PANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>

#include "engine/ArchiveEntry.h"

class FileListPanel : public wxPanel
{
public:
    FileListPanel(wxWindow* parent);

    void SetEntries(const std::vector<ArchiveEntry>& entries);
    void Clear();
    long GetSelectedIndex() const;
    wxString GetItemText(long index, int col) const;

    // Navigation
    void SetFlatMode(bool flat);
    bool IsFlatMode() const { return m_flatMode; }
    wxString GetCurrentDir() const { return m_currentDir; }
    void SetCurrentDir(const wxString& dir);
    bool NavigateInto(const wxString& subdir);
    void NavigateUp();
    bool IsSelectedDirectory() const;

    wxString GetSelectedEntryPath() const;

protected:
    void RebuildList();
    void OnItemActivated(wxListEvent&);

    int GetIconForFile(const wxString& name);

    wxListCtrl*  m_list    = nullptr;
    wxImageList* m_icons   = nullptr;
    int m_iconFolder = -1;
    int m_iconFile   = -1;
    int m_iconParent = -1;

    std::vector<ArchiveEntry> m_allEntries;

    bool m_flatMode = false;
    wxString m_currentDir;       // e.g. "subdir/subdir2" or empty (root)
    wxString m_currentDirPrefix; // e.g. "subdir/subdir2/" (with trailing /)
};

#endif
