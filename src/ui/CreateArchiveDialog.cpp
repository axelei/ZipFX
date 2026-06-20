#include "CreateArchiveDialog.h"

#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

CreateArchiveDialog::CreateArchiveDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("New Archive"),
               wxDefaultPosition, wxSize(450, 250))
{
    auto panel = new wxPanel(this);
    auto grid  = new wxFlexGridSizer(2, 10, 10);
    grid->AddGrowableCol(1);

    // Path
    grid->Add(new wxStaticText(panel, wxID_ANY, _("Save as:")),
              0, wxALIGN_CENTER_VERTICAL);
    auto pathSizer = new wxBoxSizer(wxHORIZONTAL);
    m_pathCtrl = new wxTextCtrl(panel, wxID_ANY, "");
    pathSizer->Add(m_pathCtrl, 1, wxEXPAND);
    auto browseBtn = new wxButton(panel, wxID_ANY, _("Browse..."));
    pathSizer->Add(browseBtn, 0, wxLEFT, 4);
    grid->Add(pathSizer, 1, wxEXPAND);

    // Format
    grid->Add(new wxStaticText(panel, wxID_ANY, _("Format:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_formatCombo = new wxComboBox(panel, wxID_ANY, "ZIP",
        wxDefaultPosition, wxDefaultSize,
        wxArrayString{"ZIP", "7z", "TAR.GZ"},
        wxCB_READONLY);
    grid->Add(m_formatCombo, 0, wxEXPAND);

    // Compression level
    grid->Add(new wxStaticText(panel, wxID_ANY, _("Compression:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_levelSpin = new wxSpinCtrl(panel, wxID_ANY, "6",
        wxDefaultPosition, wxSize(80, -1),
        wxSP_ARROW_KEYS, 0, 9, 6);
    grid->Add(m_levelSpin, 0, wxEXPAND);

    // Hint text
    auto hint = new wxStaticText(panel, wxID_ANY,
        _("0 = Store (fast, large)\n9 = Maximum (slow, small)"));
    grid->Add(hint);

    // Buttons
    auto btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto okBtn     = new wxButton(this, wxID_OK,     _("Create"));
    auto cancelBtn = new wxButton(this, wxID_CANCEL, _("Cancel"));
    btnSizer->Add(okBtn,     0, wxRIGHT, 8);
    btnSizer->Add(cancelBtn, 0);

    auto mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(panel,   1, wxEXPAND | wxALL, 10);
    mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 10);
    SetSizer(mainSizer);

    browseBtn->Bind(wxEVT_BUTTON, &CreateArchiveDialog::OnBrowse, this);
    okBtn->Bind(wxEVT_BUTTON,     &CreateArchiveDialog::OnOk,     this);
}

void CreateArchiveDialog::OnBrowse(wxCommandEvent&)
{
    wxString ext = "." + m_formatCombo->GetValue().Lower();
    wxFileDialog dlg(this, _("Save Archive As"), "", "",
        wxString::Format("%s (*%s)|*%s|%s (*.7z)|*.7z|%s (*.tar.gz)|*.tar.gz",
            _("ZIP Files"), ".zip", ".zip",
            _("7z Files"),
            _("TAR Files")),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dlg.ShowModal() == wxID_OK)
    {
        m_chosenPath = dlg.GetPath();
        m_pathCtrl->SetValue(m_chosenPath);
    }
}

void CreateArchiveDialog::OnOk(wxCommandEvent&)
{
    wxString path = m_pathCtrl->GetValue().Trim();
    if (path.empty())
    {
        wxMessageBox(_("Please choose a file path."), _("Missing path"),
                     wxOK | wxICON_WARNING);
        return;
    }
    m_chosenPath = path;
    EndModal(wxID_OK);
}

CreateArchiveResult CreateArchiveDialog::GetResult() const
{
    return { m_chosenPath, m_formatCombo->GetValue(), m_levelSpin->GetValue() };
}
