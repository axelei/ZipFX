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

protected:
    wxListCtrl* m_list = nullptr;
};

#endif
