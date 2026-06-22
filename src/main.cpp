#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

#include "ui/MainWindow.h"

static QTranslator* LoadAppTranslator(const QString& locale)
{
    QStringList paths = {
        QApplication::applicationDirPath() + "/translations",
        QApplication::applicationDirPath() + "/../translations",
        "translations"
    };
    auto* t = new QTranslator();
    for (const auto& dir : paths)
    {
        if (t->load(QString("zipfx_%1").arg(locale), dir))
            return t;
        if (t->load(locale, dir))
            return t;
    }
    delete t;
    return nullptr;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setOrganizationName("ZipFX");
    app.setApplicationVersion("1.0.0");

    // Load Qt's built-in translations
    {
        QTranslator* t = new QTranslator();
        if (t->load("qt_" + QLocale::system().name(),
                    QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            app.installTranslator(t);
        else
            delete t;
    }

    // Load persisted language preference, or system locale
    QSettings settings;
    QString savedLocale = settings.value("language", "").toString();
    QString locale = savedLocale.isEmpty() ? QLocale::system().name() : savedLocale;

    QTranslator* appTranslator = LoadAppTranslator(locale);
    if (appTranslator)
        app.installTranslator(appTranslator);

    MainWindow w;
    w.show();
    return app.exec();
}
