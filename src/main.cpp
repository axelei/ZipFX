#include "cli/CliHandler.h"
#include "version.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
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
    // Collect bare file arguments (not subcommands) for the GUI
    QString fileToOpen;
    bool isCli = false;

    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            if (arg == "--cli" || arg == "list" || arg == "extract" ||
                arg == "create" || arg == "test" || arg == "info" ||
                arg == "--help" || arg == "-h")
            {
                isCli = true;
                break;
            }
            // First non-subcommand argument is treated as a file path
            if (fileToOpen.isEmpty() && arg[0] != '-')
                fileToOpen = QString::fromUtf8(argv[i]);
        }
    }

    if (isCli)
        return runCli(argc, argv);

    // GUI mode – requires Qt
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setOrganizationName("ZipFX");
    app.setApplicationVersion(ZIPFX_VERSION);
    app.setWindowIcon(QIcon(app.applicationDirPath() + "/AppIcon.png"));

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

    MainWindow w(fileToOpen);
    w.show();
    return app.exec();
}
