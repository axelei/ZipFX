#ifndef ZIPFX_ICONS_H
#define ZIPFX_ICONS_H

#include <wx/wx.h>
#include <wx/bitmap.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

struct ZipFXIcons
{
    wxBitmap add;
    wxBitmap extract;
    wxBitmap test;
    wxBitmap view;
    wxBitmap del;
    wxBitmap find;
    wxBitmap wizard;
    wxBitmap info;
    wxBitmap app;
};

inline wxString GetAssetsDir()
{
    wxString dir;

    // 1. Next to the executable (deployment)
    wxFileName exePath(wxStandardPaths::Get().GetExecutablePath());
    dir = exePath.GetPath() + "/assets";
    if (wxDirExists(dir))
    {
        return dir;
    }

    // 2. Relative to working directory (development)
    dir = wxFileName::GetCwd() + "/assets";
    if (wxDirExists(dir))
    {
        return dir;
    }

    // 3. Source tree relative (CLion default working dir)
    dir = wxFileName::GetCwd() + "/../src/assets";
    if (wxDirExists(dir))
    {
        return dir;
    }

    return {};
}

inline wxBitmap LoadIcon(const wxString& name)
{
    wxString assetsDir = GetAssetsDir();
    if (!assetsDir.empty())
    {
        wxBitmap bmp;
        wxString path = assetsDir + "/" + name + ".png";
        if (bmp.LoadFile(path, wxBITMAP_TYPE_PNG))
        {
            return bmp;
        }
    }
    return {};
}

inline ZipFXIcons CreatePlaceholderIcons()
{
    ZipFXIcons icons;

    // Try loading from PNG files first
    icons.add     = LoadIcon("add");
    icons.extract = LoadIcon("extract");
    icons.test    = LoadIcon("test");
    icons.view    = LoadIcon("view");
    icons.del     = LoadIcon("delete");
    icons.find    = LoadIcon("find");
    icons.wizard  = LoadIcon("wizard");
    icons.info    = LoadIcon("info");
    icons.app     = LoadIcon("app");

    // Fallback: programmatic generation for any missing icon
    auto makeIcon = [](const wxColour& color, const wxString& letter) -> wxBitmap
    {
        wxBitmap bmp(20, 20);
        wxMemoryDC dc(bmp);
        dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_MENU)));
        dc.Clear();
        dc.SetBrush(wxBrush(color));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(1, 1, 18, 18, 4);
        dc.SetTextForeground(*wxWHITE);
        dc.SetFont(wxFontInfo(10).Bold());
        wxCoord tw, th;
        dc.GetTextExtent(letter, &tw, &th);
        dc.DrawText(letter, (20 - tw) / 2, (20 - th) / 2);
        dc.SelectObject(wxNullBitmap);
        return bmp;
    };

    if (!icons.add.IsOk())     icons.add     = makeIcon(wxColour( 76, 175,  80), "+");
    if (!icons.extract.IsOk()) icons.extract = makeIcon(wxColour( 33, 150, 243), "E");
    if (!icons.test.IsOk())    icons.test    = makeIcon(wxColour(255, 193,   7), "T");
    if (!icons.view.IsOk())    icons.view    = makeIcon(wxColour(  0, 150, 136), "V");
    if (!icons.del.IsOk())     icons.del     = makeIcon(wxColour(244,  67,  54), "X");
    if (!icons.find.IsOk())    icons.find    = makeIcon(wxColour(156,  39, 176), "F");
    if (!icons.wizard.IsOk())  icons.wizard  = makeIcon(wxColour( 63,  81, 181), "W");
    if (!icons.info.IsOk())    icons.info    = makeIcon(wxColour(  0, 188, 212), "i");
    if (!icons.app.IsOk())     icons.app     = makeIcon(wxColour(255,  87,  34), "Z");

    return icons;
}

#endif
