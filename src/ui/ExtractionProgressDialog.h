#ifndef ZIPFX_EXTRACTION_PROGRESS_DIALOG_H
#define ZIPFX_EXTRACTION_PROGRESS_DIALOG_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/button.h>

#include "PowerManager.h"

// Custom modeless progress dialog with Pause/Resume, Cancel, and After-action.
class ExtractionProgressDialog : public wxDialog
{
public:
    ExtractionProgressDialog(wxWindow* parent, int maxValue);

    void UpdateProgress(int value, const wxString& currentFile);
    bool WasCancelled() const { return m_cancelled; }
    bool WasPaused() const { return m_paused; }
    AfterAction GetAfterAction() const { return m_afterAction; }
    void SetFinished() { m_finished = true; }

private:
    void OnPause(wxCommandEvent&);
    void OnCancel(wxCommandEvent&);

    wxGauge*      m_gauge = nullptr;
    wxStaticText* m_fileLabel = nullptr;
    wxButton*     m_pauseBtn = nullptr;
    wxComboBox*   m_afterCombo = nullptr;

    bool m_cancelled = false;
    bool m_paused = false;
    bool m_finished = false;
    AfterAction m_afterAction = AfterAction::Nothing;
};

#endif
