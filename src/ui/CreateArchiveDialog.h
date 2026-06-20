#ifndef ZIPFX_CREATE_ARCHIVE_DIALOG_H
#define ZIPFX_CREATE_ARCHIVE_DIALOG_H

#include <wx/wx.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>

struct CreateArchiveResult
{
    wxString path;
    wxString format;
    int compressionLevel;
};

class CreateArchiveDialog : public wxDialog
{
public:
    CreateArchiveDialog(wxWindow* parent);

    CreateArchiveResult GetResult() const;

private:
    void OnBrowse(wxCommandEvent&);
    void OnOk(wxCommandEvent&);

    wxTextCtrl*   m_pathCtrl = nullptr;
    wxComboBox*   m_formatCombo = nullptr;
    wxSpinCtrl*   m_levelSpin = nullptr;

    wxString m_chosenPath;
};

#endif
