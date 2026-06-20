#ifndef ZIPFX_ICONS_H
#define ZIPFX_ICONS_H

#include <wx/wx.h>
#include <wx/bitmap.h>

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

inline ZipFXIcons CreatePlaceholderIcons()
{
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

    ZipFXIcons icons;
    icons.add     = makeIcon(wxColour( 76, 175,  80), "+");
    icons.extract = makeIcon(wxColour( 33, 150, 243), "E");
    icons.test    = makeIcon(wxColour(255, 193,   7), "T");
    icons.view    = makeIcon(wxColour(  0, 150, 136), "V");
    icons.del     = makeIcon(wxColour(244,  67,  54), "X");
    icons.find    = makeIcon(wxColour(156,  39, 176), "F");
    icons.wizard  = makeIcon(wxColour( 63,  81, 181), "W");
    icons.info    = makeIcon(wxColour(  0, 188, 212), "i");
    icons.app     = makeIcon(wxColour(255,  87,  34), "Z");
    return icons;
}

#endif
