#include "FileListPanel.h"

#include <wx/sizer.h>
#include <wx/artprov.h>

#include <unordered_map>

#ifdef __WXMSW__
#include <wx/icon.h>
#include <shellapi.h>
#endif

FileListPanel::FileListPanel(wxWindow* parent)
    : wxPanel(parent)
{
    m_list = new wxListCtrl(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);

    // ── Icon image list ─────────────────────────────────────────────
    // Create images from art provider and ensure proper alpha transparency
    auto makeIcon = [](wxArtID id) -> wxBitmap
    {
        wxIcon icn = wxArtProvider::GetIcon(id, wxART_LIST, wxSize(16, 16));
        wxBitmap bmp;
        bmp.CopyFromIcon(icn);
        // Ensure the bitmap has an alpha channel for proper transparency
        if (!bmp.HasAlpha())
        {
            wxImage img = bmp.ConvertToImage();
            img.InitAlpha();
            bmp = wxBitmap(img);
        }
        return bmp;
    };

    m_icons = new wxImageList(16, 16, false);

    m_iconFolder = m_icons->Add(makeIcon(wxART_FOLDER));
    m_iconFile   = m_icons->Add(makeIcon(wxART_NORMAL_FILE));
    m_iconParent = m_iconFolder;

    m_list->SetImageList(m_icons, wxIMAGE_LIST_SMALL);

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

std::vector<wxString> FileListPanel::GetSelectedEntryPaths() const
{
    std::vector<wxString> result;
    long idx = -1;
    while (true)
    {
        idx = m_list->GetNextItem(idx, wxLIST_NEXT_ALL,
                                  wxLIST_STATE_SELECTED);
        if (idx < 0) break;

        wxString name = GetItemText(idx, 0);
        if (name == "..") continue;

        result.push_back(m_currentDirPrefix + name);
    }
    return result;
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

// ── Icon lookup ────────────────────────────────────────────────────────
int FileListPanel::GetIconForFile(const wxString& name)
{
    if (name == "..")
    {
        return m_iconParent;
    }

    // No extension → use generic file icon
    wxString ext = name.AfterLast('.').Lower();
    if (name.Find('.') == wxNOT_FOUND || ext.empty())
    {
        return m_iconFile;
    }

    static std::unordered_map<wxString, int> s_extCache;

    auto it = s_extCache.find(ext);
    if (it != s_extCache.end())
    {
        return it->second;
    }

#ifdef __WXMSW__
    // Try to extract the icon from the Windows shell
    SHFILEINFOW sfi = {};
    wxString wildcard = "*." + ext;
    if (SHGetFileInfoW(wildcard.wc_str(), FILE_ATTRIBUTE_NORMAL, &sfi,
                       sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_ICON |
                       SHGFI_SMALLICON | SHGFI_SHELLICONSIZE))
    {
        wxIcon icon;
        icon.CreateFromHICON(sfi.hIcon);
        wxBitmap bmp;
        bmp.CopyFromIcon(icon);
        DestroyIcon(sfi.hIcon);
        if (!bmp.HasAlpha())
        {
            wxImage img = bmp.ConvertToImage();
            img.InitAlpha();
            bmp = wxBitmap(img);
        }
        int idx = m_icons->Add(bmp);
        s_extCache[ext] = idx;
        return idx;
    }
#else
    (void)ext;
#endif

    return m_iconFile;
}

// ── Rebuild list ───────────────────────────────────────────────────────
void FileListPanel::RebuildList()
{
    m_list->DeleteAllItems();

    if (m_flatMode)
    {
        // ── Flat mode: show everything ──
        for (const auto& e : m_allEntries)
        {
            wxString name = wxString::FromUTF8(e.name.c_str());
            int icon = e.isDirectory ? m_iconFolder : GetIconForFile(name);
            long idx = m_list->InsertItem(m_list->GetItemCount(), name, icon);

            m_list->SetItem(idx, 1, std::to_string(e.size));
            m_list->SetItem(idx, 2, std::to_string(e.packedSize));
            m_list->SetItem(idx, 3, e.isDirectory ? _("Folder") : _("File"));

            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                e.modified.time_since_epoch()).count();
            if (secs > 86400 * 365)
            {
                wxDateTime dt(static_cast<time_t>(secs));
                m_list->SetItem(idx, 4, dt.Format("%Y-%m-%d %H:%M"));
            }
            else
            {
                m_list->SetItem(idx, 4, wxString("-"));
            }

            m_list->SetItem(idx, 5, e.crc != 0
                ? wxString::Format("%08X", e.crc)
                : wxString("-"));
        }
        return;
    }

    // ── Hierarchical mode ──
    if (!m_currentDir.empty())
    {
        long idx = m_list->InsertItem(0, "..", m_iconParent);
        m_list->SetItem(idx, 1, "");
        m_list->SetItem(idx, 2, "");
        m_list->SetItem(idx, 3, _("Parent Folder"));
        m_list->SetItem(idx, 4, "");
        m_list->SetItem(idx, 5, "");
    }

    struct Child
    {
        const ArchiveEntry* entry = nullptr;
        bool isDir = false;
        wxString displayName;
    };
    std::vector<Child> children;

    for (const auto& e : m_allEntries)
    {
        const auto& ep = e.path;
        if (ep.size() < m_currentDirPrefix.size() ||
            ep.compare(0, m_currentDirPrefix.size(),
                       m_currentDirPrefix.ToStdString()) != 0)
        {
            continue;
        }

        wxString remainder = wxString::FromUTF8(ep.c_str() + m_currentDirPrefix.size());
        auto slash = remainder.Find('/');
        bool isDir = slash != wxNOT_FOUND || e.isDirectory;

        if (isDir && slash != wxNOT_FOUND)
        {
            wxString dirName = remainder.Left(slash);
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

    std::sort(children.begin(), children.end(),
        [](const Child& a, const Child& b)
        {
            if (a.isDir != b.isDir)
                return a.isDir > b.isDir;
            return a.displayName < b.displayName;
        });

    for (const auto& c : children)
    {
        int icon = c.isDir ? m_iconFolder : GetIconForFile(c.displayName);
        long idx = m_list->InsertItem(m_list->GetItemCount(), c.displayName, icon);

        if (c.entry)
        {
            m_list->SetItem(idx, 1, std::to_string(c.entry->size));
            m_list->SetItem(idx, 2, std::to_string(c.entry->packedSize));
            m_list->SetItem(idx, 3, _("File"));

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
            m_list->SetItem(idx, 1, "");
            m_list->SetItem(idx, 2, "");
            m_list->SetItem(idx, 3, _("Folder"));
            m_list->SetItem(idx, 4, "");
            m_list->SetItem(idx, 5, "");
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
