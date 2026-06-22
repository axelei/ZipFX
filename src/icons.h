#ifndef ZIPFX_ICONS_H
#define ZIPFX_ICONS_H

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QString>
#include <QDir>
#include <QApplication>
#include <QStyle>
#include <QFileInfo>

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

inline QString GetAssetsDir()
{
    // 1. Next to the executable (deployment)
    QString dir = QApplication::applicationDirPath() + "/assets";
    if (QDir(dir).exists()) return dir;

    // 2. Working directory (development)
    dir = QDir::currentPath() + "/assets";
    if (QDir(dir).exists()) return dir;

    // 3. Source tree (IDE)
    dir = QDir::currentPath() + "/../src/assets";
    if (QDir(dir).exists())
        return QDir(dir).absolutePath();

    // 4. Relative to executable parent
    dir = QApplication::applicationDirPath() + "/../src/assets";
    if (QDir(dir).exists())
        return QDir(dir).absolutePath();

    return {};
}

inline QIcon LoadIcon(const QString& name)
{
    QString assetsDir = GetAssetsDir();
    if (!assetsDir.isEmpty())
    {
        QString path = assetsDir + "/" + name + ".png";
        if (QFileInfo::exists(path))
        {
            QPixmap pm(path);
            if (!pm.isNull())
            {
                // Ensure alpha channel
                if (!pm.hasAlpha())
                    pm = QPixmap::fromImage(pm.toImage().convertToFormat(
                        QImage::Format_ARGB32_Premultiplied));
                return QIcon(pm);
            }
        }
    }
    return {};
}

inline ZipFXIcons CreatePlaceholderIcons()
{
    ZipFXIcons icons;

    icons.add     = LoadIcon("add");
    icons.extract = LoadIcon("extract");
    icons.test    = LoadIcon("test");
    icons.view    = LoadIcon("view");
    icons.del     = LoadIcon("delete");
    icons.find    = LoadIcon("find");
    icons.wizard  = LoadIcon("wizard");
    icons.info    = LoadIcon("info");
    icons.app     = LoadIcon("app");

    // Fallback: generate colored icons programmatically
    auto makeIcon = [](const QColor& color, const QString& letter) -> QIcon
    {
        QPixmap pm(24, 24);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(2, 2, 20, 20, 4, 4);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(14);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, 24, 24), Qt::AlignCenter, letter);
        p.end();
        return QIcon(pm);
    };

    if (icons.add.isNull())     icons.add     = makeIcon(QColor( 76, 175,  80), "+");
    if (icons.extract.isNull()) icons.extract = makeIcon(QColor( 33, 150, 243), "E");
    if (icons.test.isNull())    icons.test    = makeIcon(QColor(255, 193,   7), "T");
    if (icons.view.isNull())    icons.view    = makeIcon(QColor(  0, 150, 136), "V");
    if (icons.del.isNull())     icons.del     = makeIcon(QColor(244,  67,  54), "X");
    if (icons.find.isNull())    icons.find    = makeIcon(QColor(156,  39, 176), "F");
    if (icons.wizard.isNull())  icons.wizard  = makeIcon(QColor( 63,  81, 181), "W");
    if (icons.info.isNull())    icons.info    = makeIcon(QColor(  0, 188, 212), "i");
    if (icons.app.isNull())     icons.app     = makeIcon(QColor(255,  87,  34), "Z");

    return icons;
}

#endif
