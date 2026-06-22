#include "cli/CliHandler.h"
#include "version.h"

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
    // Detect CLI mode: if any argument is a known subcommand,
    // dispatch to CLI before initializing Qt
    if (argc > 1)
    {
        // --cli forces CLI mode; subcommands are positional
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            if (arg == "--cli" || arg == "list" || arg == "extract" ||
                arg == "create" || arg == "test" || arg == "info" ||
                arg == "--help" || arg == "-h")
            {
                return runCli(argc, argv);
            }
        }
    }

    // GUI mode – requires Qt
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setOrganizationName("ZipFX");
    app.setApplicationVersion(ZIPFX_VERSION);

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
