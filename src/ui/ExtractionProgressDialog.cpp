#include "ExtractionProgressDialog.h"

ExtractionProgressDialog::ExtractionProgressDialog(wxWindow* parent, int maxValue)
    : wxDialog(parent, wxID_ANY, _("Extracting"),
               wxDefaultPosition, wxSize(420, 220),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto vs = new wxBoxSizer(wxVERTICAL);

    // Progress gauge
    m_gauge = new wxGauge(this, wxID_ANY, maxValue, wxDefaultPosition,
                          wxSize(-1, 20), wxGA_HORIZONTAL);
    vs->Add(m_gauge, 0, wxEXPAND | wxALL, 10);

    // Current file label
    m_fileLabel = new wxStaticText(this, wxID_ANY, _("Preparing..."));
    vs->Add(m_fileLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // After-action combo
    auto hs1 = new wxBoxSizer(wxHORIZONTAL);
    hs1->Add(new wxStaticText(this, wxID_ANY, _("After:")), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_afterCombo = new wxComboBox(this, wxID_ANY, _("Do nothing"),
        wxDefaultPosition, wxDefaultSize, GetAfterActionLabels(), wxCB_READONLY);
    hs1->Add(m_afterCombo, 1, wxEXPAND);
    vs->Add(hs1, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Buttons
    auto hs2 = new wxBoxSizer(wxHORIZONTAL);
    m_pauseBtn = new wxButton(this, wxID_ANY, _("Pause"));
    hs2->Add(m_pauseBtn, 0, wxRIGHT, 8);
    hs2->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    vs->Add(hs2, 0, wxALIGN_CENTER | wxBOTTOM, 8);

    SetSizer(vs);
    CentreOnParent();

    m_pauseBtn->Bind(wxEVT_BUTTON, &ExtractionProgressDialog::OnPause, this);
    Bind(wxEVT_BUTTON, &ExtractionProgressDialog::OnCancel, this, wxID_CANCEL);
}

void ExtractionProgressDialog::UpdateProgress(int value, const wxString& currentFile)
{
    m_gauge->SetValue(value);
    m_fileLabel->SetLabel(currentFile);
}

void ExtractionProgressDialog::OnPause(wxCommandEvent&)
{
    m_paused = !m_paused;
    m_pauseBtn->SetLabel(m_paused ? _("Resume") : _("Pause"));

    if (m_paused)
        SetTitle(_("Extracting (paused)"));
    else
        SetTitle(_("Extracting"));
}

void ExtractionProgressDialog::OnCancel(wxCommandEvent&)
{
    m_cancelled = true;
    m_pauseBtn->Disable();
}
