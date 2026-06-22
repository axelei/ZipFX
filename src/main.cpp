#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>

#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setApplicationVersion("1.0.0");

    // Load translations
    QTranslator qtTranslator;
    if (qtTranslator.load("qt_" + QLocale::system().name(),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QTranslator appTranslator;
    if (appTranslator.load(QLocale::system().name(), "locale"))
        app.installTranslator(&appTranslator);

    MainWindow w;
    w.show();
    return app.exec();
}
