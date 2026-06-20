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

    m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &FileListPanel::OnItemActivated, this);

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_list, 1, wxEXPAND);
    SetSizer(sizer);
}

// ── Data ───────────────────────────────────────────────────────────────
void FileListPanel::SetEntries(const std::vector<ArchiveEntry>& entries)
{
    m_allEntries = entries;
    RebuildList();
}

void FileListPanel::Clear()
{
    m_allEntries.clear();
    m_currentDir.clear();
    m_currentDirPrefix.clear();
    m_list->DeleteAllItems();
}

// ── Mode ───────────────────────────────────────────────────────────────
void FileListPanel::SetFlatMode(bool flat)
{
    m_flatMode = flat;
    RebuildList();
}

void FileListPanel::SetCurrentDir(const wxString& dir)
{
    m_currentDir = dir;
    if (dir.empty() || dir.Last() == '/')
    {
        m_currentDirPrefix = dir;
    }
    else
    {
        m_currentDirPrefix = dir + "/";
    }
    RebuildList();
}

bool FileListPanel::NavigateInto(const wxString& subdir)
{
    wxString newDir = m_currentDirPrefix + subdir;
    // Check that at least one entry starts with this prefix
    wxString prefix = newDir + "/";
    for (const auto& e : m_allEntries)
    {
        if (e.path.compare(0, prefix.size(), prefix.ToStdString()) == 0)
        {
            SetCurrentDir(newDir);
            return true;
        }
    }
    return false;
}

void FileListPanel::NavigateUp()
{
    if (m_currentDir.empty())
    {
        return;
    }
    auto pos = m_currentDir.Find('/', true);
    if (pos == wxNOT_FOUND)
    {
        SetCurrentDir("");
    }
    else
    {
        SetCurrentDir(m_currentDir.Left(pos));
    }
}

// ── Selection helpers ──────────────────────────────────────────────────
long FileListPanel::GetSelectedIndex() const
{
    return m_list->GetNextItem(-1, wxLIST_NEXT_ALL,
                               wxLIST_STATE_SELECTED);
}

wxString FileListPanel::GetItemText(long index, int col) const
{
    return m_list->GetItemText(index, col);
}

wxString FileListPanel::GetSelectedEntryPath() const
{
    long idx = GetSelectedIndex();
    if (idx < 0)
    {
        return {};
    }

    wxString name = GetItemText(idx, 0);

    // ".." entry maps to parent directory
    if (name == "..")
    {
        if (m_currentDir.empty())
        {
            return {};
        }
        auto pos = m_currentDir.Find('/', true);
        if (pos == wxNOT_FOUND)
        {
            return {};
        }
        return m_currentDir.Left(pos);
    }

    return m_currentDirPrefix + name;
}

bool FileListPanel::IsSelectedDirectory() const
{
    long idx = GetSelectedIndex();
    if (idx < 0)
    {
        return false;
    }

    wxString name = GetItemText(idx, 0);
    if (name == "..")
    {
        return true;
    }

    // Check in all entries
    wxString fullPath = m_currentDirPrefix + name;
    for (const auto& e : m_allEntries)
    {
        if (e.path == fullPath.ToStdString())
        {
            return e.isDirectory;
        }
    }

    // If it's a prefix of other entries, treat as directory
    for (const auto& e : m_allEntries)
    {
        if (e.path.size() > fullPath.size() &&
            e.path.compare(0, fullPath.size(), fullPath.ToStdString()) == 0 &&
            e.path[fullPath.size()] == '/')
        {
            return true;
        }
    }

    return false;
}

// ── Rebuild list ───────────────────────────────────────────────────────
void FileListPanel::RebuildList()
{
    m_list->DeleteAllItems();

    std::vector<const ArchiveEntry*> displayEntries;

    if (m_flatMode)
    {
        // Show everything flat
        for (const auto& e : m_allEntries)
        {
            displayEntries.push_back(&e);
        }
    }
    else
    {
        // Show only direct children of current directory
        if (!m_currentDir.empty())
        {
            // Add ".." entry for navigation
            long idx = m_list->InsertItem(0, "..");
            m_list->SetItem(idx, 1, "");
            m_list->SetItem(idx, 2, "");
            m_list->SetItem(idx, 3, _("Parent Folder"));
            m_list->SetItem(idx, 4, "");
            m_list->SetItem(idx, 5, "");
        }

        // Collect immediate children (entries at this level)
        struct Child
        {
            const ArchiveEntry* entry = nullptr;
            bool isDir = false;
            wxString displayName;
        };
        std::vector<Child> children;

        for (const auto& e : m_allEntries)
        {
            // Must start with prefix
            const auto& ep = e.path;
            if (ep.size() < m_currentDirPrefix.size() ||
                ep.compare(0, m_currentDirPrefix.size(),
                           m_currentDirPrefix.ToStdString()) != 0)
            {
                continue;
            }

            wxString remainder = wxString::FromUTF8(ep.c_str() + m_currentDirPrefix.size());

            // Remove everything after the first /
            auto slash = remainder.Find('/');
            bool isDir = slash != wxNOT_FOUND || e.isDirectory;

            if (isDir && slash != wxNOT_FOUND)
            {
                // It's a subdirectory — truncate to just the dir name
                wxString dirName = remainder.Left(slash);

                // Deduplicate: check if we already added this subdirectory
                bool alreadyAdded = false;
                for (const auto& c : children)
                {
                    if (c.displayName == dirName)
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (!alreadyAdded)
                {
                    children.push_back({nullptr, true, dirName});
                }
            }
            else if (!e.isDirectory)
            {
                children.push_back({&e, false, remainder});
            }
        }

        // Sort: directories first, then files, alphabetically
        std::sort(children.begin(), children.end(),
            [](const Child& a, const Child& b)
            {
                if (a.isDir != b.isDir)
                {
                    return a.isDir > b.isDir; // dirs first
                }
                return a.displayName < b.displayName;
            });

        for (const auto& c : children)
        {
            long idx = m_list->InsertItem(m_list->GetItemCount(), c.displayName);

            if (c.entry)
            {
                m_list->SetItem(idx, 1, std::to_string(c.entry->size));
                m_list->SetItem(idx, 2, std::to_string(c.entry->packedSize));
                m_list->SetItem(idx, 3, c.entry->isDirectory
                    ? _("Folder") : _("File"));

                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    c.entry->modified.time_since_epoch()).count();
                if (secs > 86400 * 365)
                {
                    wxDateTime dt(static_cast<time_t>(secs));
                    m_list->SetItem(idx, 4, dt.Format("%Y-%m-%d %H:%M"));
                }
                else
                {
                    m_list->SetItem(idx, 4, wxString("-"));
                }

                m_list->SetItem(idx, 5, c.entry->crc != 0
                    ? wxString::Format("%08X", c.entry->crc)
                    : wxString("-"));
            }
            else
            {
                // Directory placeholder — show aggregated info
                m_list->SetItem(idx, 1, "");
                m_list->SetItem(idx, 2, "");
                m_list->SetItem(idx, 3, _("Folder"));
                m_list->SetItem(idx, 4, "");
                m_list->SetItem(idx, 5, "");
            }
        }
    }
}

// ── Double-click ───────────────────────────────────────────────────────
void FileListPanel::OnItemActivated(wxListEvent&)
{
    if (m_flatMode)
    {
        return;
    }

    long idx = GetSelectedIndex();
    if (idx < 0)
    {
        return;
    }

    wxString name = GetItemText(idx, 0);

    if (name == "..")
    {
        NavigateUp();
    }
    else if (IsSelectedDirectory())
    {
        NavigateInto(name);
    }
}
