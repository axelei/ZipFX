#include "cli/CliHandler.h"
#include "version.h"

#ifdef _WIN32
#  include <windows.h>
#  include <cstdio>
#endif

#ifndef _WIN32
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
static void crashHandler(int sig)
{
    void* frames[64];
    int n = backtrace(frames, 64);
    const char* msg = (sig == SIGSEGV) ? "\n*** SIGSEGV — backtrace:\n" : "\n*** SIGABRT — backtrace:\n";
    write(STDERR_FILENO, msg, __builtin_strlen(msg));
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QSharedMemory>
#include <QTimer>

#include "ui/MainWindow.h"

static QTranslator* LoadAppTranslator(const QString& locale)
{
    QStringList paths = {
        QApplication::applicationDirPath() + "/translations",
        QApplication::applicationDirPath() + "/../translations",
        QApplication::applicationDirPath() + "/../Resources/translations",
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

// ── Single-instance ────────────────────────────────────────────────────
// Uses QSharedMemory to detect an existing instance and QLocalServer to
// forward file paths to the running window.

static const char* kSharedMemKey = "ZipFX-SingleInstance";
static const char* kLocalServerName = "ZipFX-LocalServer";

static bool tryActivateExistingInstance(const QString& fileToOpen)
{
    QSharedMemory sharedMem(kSharedMemKey);
    if (!sharedMem.attach())
        return false; // No existing instance found

    // Send the file path to the existing instance via local socket
    QLocalSocket socket;
    socket.connectToServer(kLocalServerName);
    if (!socket.waitForConnected(1000))
        return false;

    QByteArray data = fileToOpen.toUtf8();
    socket.write(data);
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

// ── Main ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
#ifndef _WIN32
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
#endif
    // Collect bare file arguments (not subcommands) for the GUI
    QString     fileToOpen;
    QStringList shellAddFiles;
    bool        isCli      = false;
    bool        isShellAdd = false;

    if (argc > 1)
    {
        if (std::string(argv[1]) == "--shell-add")
        {
            isShellAdd = true;
            for (int i = 2; i < argc; ++i)
                shellAddFiles << QString::fromUtf8(argv[i]);
        }
        else
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
    }

    if (isCli)
    {
#ifdef _WIN32
        // GUI executables on Windows have no console attached.
        // Attach to the parent console (cmd/PowerShell) so that stdout/stderr
        // are visible. If there is no parent console, allocate a new one.
        bool attachedToParent = AttachConsole(ATTACH_PARENT_PROCESS);
        if (!attachedToParent)
            AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$",  "r", stdin);
#endif
        // Strip --cli from argv before passing to runCli
        int newArgc = 0;
        const char* newArgv[64];
        for (int i = 0; i < argc && newArgc < 64; ++i)
            if (std::string(argv[i]) != "--cli")
                newArgv[newArgc++] = argv[i];
        // QCoreApplication gives Qt internals (e.g. applicationDirPath) a
        // valid app object without initialising any GUI subsystem.
        QCoreApplication coreApp(newArgc, const_cast<char**>(newArgv));
        int result = runCli(newArgc, const_cast<char**>(newArgv));
#ifdef _WIN32
        // When attached to a parent console (cmd/PowerShell), the shell's
        // prompt was already printed before our output. Inject a synthetic
        // Enter so the shell redraws the prompt without requiring user input.
        if (attachedToParent)
        {
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            INPUT_RECORD ir[2] = {};
            ir[0].EventType = KEY_EVENT;
            ir[0].Event.KeyEvent.bKeyDown          = TRUE;
            ir[0].Event.KeyEvent.wVirtualKeyCode   = VK_RETURN;
            ir[0].Event.KeyEvent.wVirtualScanCode  = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
            ir[0].Event.KeyEvent.uChar.UnicodeChar = L'\r';
            ir[0].Event.KeyEvent.dwControlKeyState = 0;
            ir[1]                                  = ir[0];
            ir[1].Event.KeyEvent.bKeyDown          = FALSE;
            DWORD written = 0;
            WriteConsoleInputW(hIn, ir, 2, &written);
        }
#endif
        return result;
    }

    // Shell-add always opens a new instance (no single-instance redirect)
    if (!isShellAdd && tryActivateExistingInstance(fileToOpen))
    {
        qDebug("Forwarded file to existing ZipFX instance, exiting");
        return 0;
    }

    // Auto-select Wayland platform on Wayland sessions (for proper DND).
    // Respect explicit QT_QPA_PLATFORM override if set.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "wayland");

    // GUI mode – requires Qt
    QApplication app(argc, argv);
    app.setApplicationName("ZipFX");
    app.setOrganizationName("ZipFX");
    app.setApplicationVersion(ZIPFX_VERSION);
    {
        // Prefer .ico on Windows (higher resolution, already installed next to exe).
        // Fall back to .png for other platforms.
        const QString dir = app.applicationDirPath();
        QIcon icon;
#ifdef _WIN32
        if (QFile::exists(dir + "/AppIcon.ico"))
            icon = QIcon(dir + "/AppIcon.ico");
#endif
        if (icon.isNull())
            icon = QIcon(dir + "/AppIcon.png");
        app.setWindowIcon(icon);
    }

    // Create shared memory to own the instance
    auto* sharedMem = new QSharedMemory(kSharedMemKey);
    sharedMem->create(1);

    // Local server to receive file paths from other instances
    auto* localServer = new QLocalServer();
    QLocalServer::removeServer(kLocalServerName);
    localServer->listen(kLocalServerName);

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

    // Heap-allocate so WA_DeleteOnClose (set in the constructor) can safely
    // free the window when it's closed while other windows are still open.
    // A stack-allocated window would be double-freed in that case (SIGABRT).
    auto* w = new MainWindow(fileToOpen);
    w->show();

    if (isShellAdd && !shellAddFiles.isEmpty())
        QTimer::singleShot(0, [w, shellAddFiles]() { w->shellAdd(shellAddFiles); });

    // Handle file paths forwarded from other instances — open each in a new window
    QObject::connect(localServer, &QLocalServer::newConnection, [&]() {
        QLocalSocket* client = localServer->nextPendingConnection();
        if (!client) return;
        client->waitForReadyRead(1000);
        QString path = QString::fromUtf8(client->readAll());
        if (!path.isEmpty()) {
            auto* w2 = new MainWindow(path);
            w2->show();
        }
        client->deleteLater();
    });

    return app.exec();
}
