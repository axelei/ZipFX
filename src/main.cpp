#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>

#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setApplicationVersion("1.0.0");

    // Load Qt's built-in translations
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + QLocale::system().name(),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load application translations
    QTranslator appTranslator;
    QString locale = QLocale::system().name();
    if (appTranslator.load(QString("zipfx_%1").arg(locale), "translations"))
        app.installTranslator(&appTranslator);
    else if (appTranslator.load(locale, "translations"))
        app.installTranslator(&appTranslator);

    MainWindow w;
    w.show();
    return app.exec();
}
