#ifndef ZIPFX_ICONS_H
#define ZIPFX_ICONS_H

#include <QIcon>
#include <QApplication>
#include <QStyle>

struct ZipFXIcons
{
    QIcon add;
    QIcon extract;
    QIcon test;
    QIcon view;
    QIcon del;
    QIcon find;
    QIcon wizard;
    QIcon info;
    QIcon app;
};

inline QIcon StdIcon(QStyle::StandardPixmap sp)
{
    return QApplication::style()->standardIcon(sp);
}

inline ZipFXIcons CreatePlaceholderIcons()
{
    ZipFXIcons icons;

    icons.add     = StdIcon(QStyle::SP_FileDialogNewFolder);
    icons.extract = StdIcon(QStyle::SP_DialogSaveButton);
    icons.test    = StdIcon(QStyle::SP_MediaPlay);
    icons.view    = StdIcon(QStyle::SP_FileIcon);
    icons.del     = StdIcon(QStyle::SP_TrashIcon);
    icons.find    = StdIcon(QStyle::SP_FileDialogContentsView);
    icons.wizard  = StdIcon(QStyle::SP_FileDialogStart);
    icons.info    = StdIcon(QStyle::SP_MessageBoxInformation);
    icons.app     = StdIcon(QStyle::SP_ComputerIcon);

    return icons;
}

#endif
