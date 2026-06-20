#include "FileListPanel.h"

#include <wx/sizer.h>

FileListPanel::FileListPanel(wxWindow* parent)
    : wxPanel(parent)
{
    m_list = new wxListCtrl(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);

    m_list->AppendColumn(_("Name"),     wxLIST_FORMAT_LEFT,  240);
    m_list->AppendColumn(_("Size"),     wxLIST_FORMAT_RIGHT,  90);
    m_list->AppendColumn(_("Packed"),   wxLIST_FORMAT_RIGHT,  90);
    m_list->AppendColumn(_("Type"),     wxLIST_FORMAT_LEFT,  140);
    m_list->AppendColumn(_("Modified"), wxLIST_FORMAT_LEFT,  140);
    m_list->AppendColumn(_("CRC"),      wxLIST_FORMAT_LEFT,   80);

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_list, 1, wxEXPAND);
    SetSizer(sizer);
}

void FileListPanel::SetEntries(const std::vector<ArchiveEntry>& entries)
{
    m_list->DeleteAllItems();

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        long idx = m_list->InsertItem(static_cast<long>(i), e.name);

        m_list->SetItem(idx, 1, std::to_string(e.size));
        m_list->SetItem(idx, 2, std::to_string(e.packedSize));
        m_list->SetItem(idx, 3, e.isDirectory ? _("Folder") : _("File"));
        {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                e.modified.time_since_epoch()).count();
            if (secs > 86400 * 365) // skip epoch / unknown dates
            {
                wxDateTime dt(static_cast<time_t>(secs));
                m_list->SetItem(idx, 4, dt.Format("%Y-%m-%d %H:%M"));
            }
            else
            {
                m_list->SetItem(idx, 4, wxString("-"));
            }
        }
        m_list->SetItem(idx, 5, e.crc != 0
            ? wxString::Format("%08X", e.crc)
            : wxString("-"));
    }
}

void FileListPanel::Clear()
{
    m_list->DeleteAllItems();
}

long FileListPanel::GetSelectedIndex() const
{
    return m_list->GetNextItem(-1, wxLIST_NEXT_ALL,
                               wxLIST_STATE_SELECTED);
}

wxString FileListPanel::GetItemText(long index, int col) const
{
    return m_list->GetItemText(index, col);
}
