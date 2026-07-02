#include "MainWindow.h"
#include "ArchiveProgress.h"
#include "PreviewPanel.h"
#include "BatchOpsDialog.h"
#include "PasswordManagerDialog.h"
#include "FileListModel.h"
#include "CreateArchiveDialog.h"
#include "ChecksumsDialog.h"
#include "FindFilesDialog.h"
#include "KeychainHelper.h"
#include "recovery/RecoveryRecord.h"
#include "icons.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "ProgressInfo.h"
#include "PowerManager.h"
#include "engine/Logging.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>

#include <QThread>
#include <QTimer>
#include <QDesktopServices>
#include <QClipboard>
#include <QDirIterator>
#include <QFile>
#include <QFormLayout>
#include <QProcess>
#include <QSplitter>
#include <QHeaderView>
#include <QTextEdit>
#include <QDialogButtonBox>

#include <atomic>
#include <thread>
#include <chrono>
#ifdef Q_OS_MACOS
#include "dnd/MacPromiseDrag.h"
#endif
#include "dnd/TempCleanup.h"
#ifdef ZIPFX_HAVE_FUSE
#include "dnd/FuseArchiveMount.h"
#endif
#include <QDropEvent>
#include <QFileOpenEvent>
#include <QUrl>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QPushButton>
#include <QDateTime>
#include <QRandomGenerator>
#include <QProgressBar>
#include <QProgressDialog>
#include <QInputDialog>
#include <QFontDatabase>
#include <QPixmap>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QActionGroup>
#include <QSettings>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveExtensions.h"
#include "engine/ArchiveEntry.h"
#include "engine/Bit7zEngine.h"

#include "version.h"

#include <filesystem>
#include <algorithm>
#include <set>
namespace fs = std::filesystem;

// Helper: re-fits a graphics view when the parent widget resizes
class ResizeEventFilter : public QObject
{
public:
    ResizeEventFilter(std::function<void()> onResize, QObject* parent = nullptr)
        : QObject(parent), m_onResize(std::move(onResize)) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::Resize && m_onResize)
            m_onResize();
        return QObject::eventFilter(obj, event);
    }
private:
    std::function<void()> m_onResize;
};

#ifdef _WIN32
#include "dnd/VirtualFileDataObject.h"
#endif

// ── Helper ─────────────────────────────────────────────────────────────
static QTranslator* loadTranslator(const QString& locale)
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

// ── Constructor ────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("ZipFX %1").arg(ZIPFX_VERSION));
    resize(960, 640);

    m_icons = new ZipFXIcons(CreatePlaceholderIcons());

    setupMenus();
    setupToolbar();
    setupUI();

    // Enable drops on the main window
    setAcceptDrops(true);

    // Status bar
    statusBar()->showMessage(tr("Ready"));

#ifdef _WIN32
    registerFileAssociations();
#elif defined(__APPLE__)
    registerFileAssociationsMac();
    // QFileOpenEvent is dispatched to qApp, not to windows — install a filter
    // so Finder "Open With" / double-click reaches this window.
    qApp->installEventFilter(this);
#elif defined(__linux__)
    registerFileAssociationsLinux();
#endif
}

MainWindow::MainWindow(const QString& fileToOpen, QWidget* parent)
    : MainWindow(parent)
{
    if (!fileToOpen.isEmpty())
        openArchive(fileToOpen);
}

MainWindow::~MainWindow()
{
    clearSensitiveData();
    qApp->removeEventFilter(this);
#ifdef ZIPFX_HAVE_FUSE
    if (m_fuseThread.joinable())
    {
        m_fuseMount->abandon();
        m_fuseThread.join();
    }
    m_fuseMount.reset();
#endif
    onCloseArchive();
    delete m_icons;
    if (m_currentTranslator)
    {
        qApp->removeTranslator(m_currentTranslator);
        delete m_currentTranslator;
    }
}

void MainWindow::clearSensitiveData()
{
    if (!m_archivePassword.empty())
    {
        // A plain loop over a non-volatile pointer can be eliminated by the
        // optimizer (especially under LTO) since the buffer is about to be
        // destroyed anyway. Use the platform's guaranteed-not-elided zeroing
        // primitive instead of relying on `volatile`.
#ifdef _WIN32
        SecureZeroMemory(m_archivePassword.data(), m_archivePassword.size());
#elif defined(__APPLE__)
        memset_s(m_archivePassword.data(), m_archivePassword.size(), 0, m_archivePassword.size());
#else
        explicit_bzero(m_archivePassword.data(), m_archivePassword.size());
#endif
        m_archivePassword.clear();
    }
}

// ── UI Setup ───────────────────────────────────────────────────────────
void MainWindow::setupMenus()
{
    // File
    auto fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("New &Window\tCtrl+Shift+N"), this, [this]() {
        auto* w = new MainWindow({});
        w->show();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&New Archive...\tCtrl+N"), this, &MainWindow::onNewArchive);
    fileMenu->addAction(tr("&Open Archive...\tCtrl+O"), this, &MainWindow::onOpenArchive);
    fileMenu->addAction(tr("&Close Archive\tCtrl+C"), this, &MainWindow::onCloseArchive);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xtract to...\tCtrl+E"), this, &MainWindow::onExtractAll);
    fileMenu->addAction(tr("Extract &Here"), this, [this]() {
        if (!m_engine) return;
        QFileInfo fi(QString::fromStdString(m_currentPath));
        QString archiveName = fi.completeBaseName();
        if (archiveName.endsWith(".tar", Qt::CaseInsensitive))
            archiveName = QFileInfo(archiveName).completeBaseName();
        QString destPath = fi.dir().filePath(archiveName);
        QDir().mkpath(destPath);
        doExtract(destPath, true, false);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit\tAlt+F4"), this, &QMainWindow::close);

    // Edit
    auto editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("Select &All\tCtrl+A"), this, [this]() {
        if (m_treeView) m_treeView->selectAll();
    });
    editMenu->addAction(tr("&Invert Selection"), this, [this]() {
        if (!m_model || !m_treeView) return;
        auto* sel = m_treeView->selectionModel();
        int rows = m_model->rowCount();
        for (int row = 0; row < rows; ++row) {
            QModelIndex idx = m_model->index(row, 0);
            sel->select(idx, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
        }
    });

    // Commands
    auto cmdMenu = menuBar()->addMenu(tr("&Commands"));
    cmdMenu->addAction(tr("&Add Files...\tAlt+A"), this, &MainWindow::onAddFiles);
    cmdMenu->addAction(tr("Add Fol&der...\tAlt+D"), this, &MainWindow::onAddFolder);
    cmdMenu->addAction(tr("E&xtract...\tAlt+E"), this, &MainWindow::onExtractAll);
    cmdMenu->addAction(tr("Extract without &Paths..."), this, [this]() {
        if (!m_engine) return;
        QString dest = QFileDialog::getExistingDirectory(this, tr("Extract without paths to"));
        if (dest.isEmpty()) return;
        doExtract(dest, true, true);
    });
    cmdMenu->addAction(tr("&Test\tAlt+T"), this, &MainWindow::onTest);
    cmdMenu->addAction(tr("&View\tAlt+V"), this, &MainWindow::onView);
    cmdMenu->addAction(tr("&Delete\tDel"), this, &MainWindow::onDelete);
    cmdMenu->addSeparator();
    m_setPasswordAct = cmdMenu->addAction(tr("Set &Password..."), this, [this]() {
        if (!m_engine) return;

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Set Password"));
        dlg.setMinimumWidth(320);
        auto* lay = new QFormLayout(&dlg);
        lay->setContentsMargins(12, 12, 12, 12);
        lay->setSpacing(8);

        auto* pwdEdit     = new QLineEdit(&dlg);
        pwdEdit->setEchoMode(QLineEdit::Password);
        pwdEdit->setText(QString::fromStdString(m_archivePassword));
        auto* confirmEdit = new QLineEdit(&dlg);
        confirmEdit->setEchoMode(QLineEdit::Password);
        auto* matchLabel  = new QLabel(&dlg);
        matchLabel->setStyleSheet(QStringLiteral("color: red; font-size: 10px;"));
        matchLabel->hide();

        lay->addRow(tr("Password:"),         pwdEdit);
        lay->addRow(tr("Confirm password:"), confirmEdit);
        lay->addRow(QString(), matchLabel);

        auto* btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        lay->addRow(btns);

        connect(btns, &QDialogButtonBox::accepted, &dlg, [&]() {
            if (pwdEdit->text() != confirmEdit->text()) {
                matchLabel->setText(tr("Passwords do not match."));
                matchLabel->show();
                return;
            }
            dlg.accept();
        });
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;

        QString pwd = pwdEdit->text();
        m_archivePassword = pwd.toStdString();
        m_engine->setPassword(m_archivePassword);
        if (!pwd.isEmpty() && !m_currentPath.empty())
        {
            auto ans = QMessageBox::question(this, tr("Save Password"),
                tr("Remember this password for this archive?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ans == QMessageBox::Yes)
                savePassword(QFileInfo(QString::fromStdString(m_currentPath)).absoluteFilePath(), pwd);
        }
        pwd.fill(QChar(0));
    });
    m_setPasswordAct->setEnabled(false);
    cmdMenu->addAction(tr("E&xclude Patterns..."), this, [this]() {
        bool ok;
        QString pats = QInputDialog::getText(this, tr("Exclude Patterns"),
            tr("Patterns to skip when adding files (comma-separated, e.g. *.tmp,thumbs.db):"),
            QLineEdit::Normal, m_excludePatterns.join(","), &ok);
        if (ok)
            m_excludePatterns = pats.isEmpty()
                ? QStringList()
                : pats.split(",", Qt::SkipEmptyParts);
    });
    cmdMenu->addSeparator();
    cmdMenu->addAction(tr("&Find...\tF3"), this, [this]() {
        m_searchBox->setVisible(!m_searchBox->isVisible());
        if (m_searchBox->isVisible()) m_searchBox->setFocus();
        else m_model->setFilterString(QString());
    });

    cmdMenu->addAction(tr("&Information...\tCtrl+I"), this, &MainWindow::onInfo);
    m_archiveCommentAct = cmdMenu->addAction(tr("Archive &Comment..."), this, &MainWindow::onArchiveComment);
    m_archiveCommentAct->setEnabled(false); // enabled when a supporting format is open
    cmdMenu->addSeparator();
    cmdMenu->addAction(tr("Find Fi&les...\tCtrl+F"), this, &MainWindow::onFindFiles);
    cmdMenu->addAction(tr("Con&vert Archive..."),    this, &MainWindow::onConvertArchive);
    cmdMenu->addAction(tr("Re&pair Archive..."),     this, &MainWindow::onRepairArchive);
    cmdMenu->addAction(tr("Batch &Operations..."),   this, &MainWindow::onBatchOps);
    cmdMenu->addSeparator();
    cmdMenu->addAction(tr("Add &Recovery Record..."),    this, &MainWindow::onAddRecoveryRecord);
    cmdMenu->addAction(tr("&Verify/Repair with Recovery Record..."), this, &MainWindow::onVerifyRecoveryRecord);
    cmdMenu->addSeparator();
    m_passwordAct = cmdMenu->addAction(tr("Password &Manager..."), this, &MainWindow::onPasswordManager);

    // Options
    auto optsMenu = menuBar()->addMenu(tr("&Options"));
    m_flatAct = optsMenu->addAction(tr("&Flat File List"));
    m_flatAct->setCheckable(true);
    connect(m_flatAct, &QAction::toggled, this, [this](bool checked) {
        m_model->setFlatMode(checked);
        // Up button makes sense only in hierarchical mode when inside a subdir
        m_upBtn->setVisible(!checked && !m_model->currentDir().isEmpty());
        QSettings().setValue("options/flatMode", checked);
    });

    QAction* previewAct = optsMenu->addAction(tr("Show &Preview Pane"));
    previewAct->setCheckable(true);
    previewAct->setChecked(false);
    connect(previewAct, &QAction::toggled, this, [this](bool checked) {
        if (m_previewPanel) {
            m_previewPanel->setVisible(checked);
            if (checked) updatePreview();
        }
    });

    {
        QSettings s;
        m_keepBrokenFiles = s.value("options/keepBrokenFiles", false).toBool();
    }
#ifdef _WIN32
    optsMenu->addSeparator();
    optsMenu->addAction(tr("Install Shell &Extension (Explorer right-click)"), this,
        [this]() { installShellExtension(true); });
    optsMenu->addAction(tr("&Uninstall Shell Extension"), this,
        [this]() { installShellExtension(false); });
    optsMenu->addSeparator();
#endif

    QAction* keepBrokenAct = optsMenu->addAction(tr("&Keep Broken Files on Extraction"));
    keepBrokenAct->setCheckable(true);
    keepBrokenAct->setChecked(m_keepBrokenFiles);
    connect(keepBrokenAct, &QAction::toggled, this, [this](bool checked) {
        m_keepBrokenFiles = checked;
        QSettings s;
        s.setValue("options/keepBrokenFiles", checked);
    });

    {
        QSettings s;
        m_openAfterExtract = s.value("options/openAfterExtract", false).toBool();
    }
    QAction* openAfterAct = optsMenu->addAction(tr("Open &Destination Folder After Extraction"));
    openAfterAct->setCheckable(true);
    openAfterAct->setChecked(m_openAfterExtract);
    connect(openAfterAct, &QAction::toggled, this, [this](bool checked) {
        m_openAfterExtract = checked;
        QSettings().setValue("options/openAfterExtract", checked);
    });

    optsMenu->addSeparator();
    auto langMenu = optsMenu->addMenu(tr("&Language"));
    QActionGroup* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);

    auto addLang = [&](const QString& flag, const QString& name, const QString& locale) {
        QAction* act = langMenu->addAction(flag + "  " + name);
        act->setData(locale);
        act->setCheckable(true);
        act->setChecked(locale == QLocale::system().name().left(2));
        langGroup->addAction(act);
    };
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7"), "English", "en");     // 🇬🇧
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAA\xF0\x9F\x87\xB8"), "Espa\u00F1ol", "es"); // 🇪🇸
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7"), "Fran\u00E7ais", "fr"); // 🇫🇷
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9"), "Italiano", "it");     // 🇮🇹
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB5\xF0\x9F\x87\xB9"), "Portugu\u00EAs", "pt"); // 🇵🇹
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB3\xF0\x9F\x87\xB1"), "Nederlands", "nl");  // 🇳🇱
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB8\xF0\x9F\x87\xAA"), "Svenska", "sv");     // 🇸🇪
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB3\xF0\x9F\x87\xB4"), "Norsk", "no");       // 🇳🇴
    addLang(QString::fromUtf8("\xF0\x9F\x87\xA9\xF0\x9F\x87\xB0"), "Dansk", "da");       // 🇩🇰
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAB\xF0\x9F\x87\xAE"), "Suomi", "fi");       // 🇫🇮
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA"), "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", "ru"); // 🇷🇺
    addLang(QString::fromUtf8("\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"), "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "ja"); // 🇯🇵
    addLang(QString::fromUtf8("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3"), "\xE4\xB8\xAD\xE6\x96\x87", "zh"); // 🇨🇳
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7"), "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", "ko"); // 🇰🇷
    addLang(QString::fromUtf8("\xF0\x9F\x87\xB8\xF0\x9F\x87\xA6"), "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9", "ar"); // 🇸🇦

    connect(langGroup, &QActionGroup::triggered, this, [this](QAction* act) {
        QString locale = act->data().toString();

        // Save preference
        QSettings settings;
        settings.setValue("language", locale);
        settings.sync();

        // Try loading the new translator
        QTranslator* translator = loadTranslator(locale);
        if (translator)
        {
            if (m_currentTranslator)
            {
                qApp->removeTranslator(m_currentTranslator);
                delete m_currentTranslator;
            }
            m_currentTranslator = translator;
            qApp->installTranslator(translator);
            QMessageBox::information(this, tr("Language"),
                tr("Language changed to %1.\nRestart the app for the change to take full effect.")
                    .arg(act->text()));
        }
    });

    // Help
    auto helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About ZipFX"), this, [this]() {
        QMessageBox::about(this, tr("About ZipFX"),
            tr("ZipFX v%1\n\nMultiplatform archiver for power users.\n"
               "Supported: ZIP, 7z, RAR, TAR.GZ, ISO, CAB, LHA, XAR, CPIO, ...")
            .arg(ZIPFX_VERSION));
    });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&License"), this, [this]() {
        QStringList paths = {
            QApplication::applicationDirPath() + "/LICENSE",
            QApplication::applicationDirPath() + "/../LICENSE",
            QApplication::applicationDirPath() + "/../share/doc/zipfx/LICENSE",
        };
        QFile file;
        for (const auto &p : paths) {
            file.setFileName(p);
            if (file.exists()) break;
        }
        QString text;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            text = QString::fromUtf8(file.readAll());
        else
            text = tr("(LICENSE file not found)");

        QDialog dlg(this);
        dlg.setWindowTitle(tr("License"));
        dlg.resize(640, 480);
        auto *layout = new QVBoxLayout(&dlg);
        auto *editor = new QTextEdit(&dlg);
        editor->setPlainText(text);
        editor->setReadOnly(true);
        layout->addWidget(editor);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);
        dlg.exec();
    });
}

void MainWindow::setupToolbar()
{
    m_toolbar = addToolBar(tr("Tools"));
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto newAct = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileIcon), tr("New"));
    connect(newAct, &QAction::triggered, this, &MainWindow::onNewArchive);

    auto openAct = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open"));
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenArchive);

    m_toolbar->addSeparator();

    auto addAct = m_toolbar->addAction(m_icons->add, tr("Add"));
    connect(addAct, &QAction::triggered, this, &MainWindow::onAddFiles);

    auto addDirAct = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder), tr("Add Folder"));
    connect(addDirAct, &QAction::triggered, this, &MainWindow::onAddFolder);

    auto extractAct = m_toolbar->addAction(m_icons->extract, tr("Extract..."));
    connect(extractAct, &QAction::triggered, this, &MainWindow::onExtractAll);

    auto testAct = m_toolbar->addAction(m_icons->test, tr("Test"));
    connect(testAct, &QAction::triggered, this, &MainWindow::onTest);

    auto viewAct = m_toolbar->addAction(m_icons->view, tr("View"));
    connect(viewAct, &QAction::triggered, this, &MainWindow::onView);

    m_toolbar->addSeparator();

    auto delAct = m_toolbar->addAction(m_icons->del, tr("Delete"));
    connect(delAct, &QAction::triggered, this, &MainWindow::onDelete);

    m_toolbar->addSeparator();

    auto closeAct = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_DialogCloseButton), tr("Close"));
    connect(closeAct, &QAction::triggered, this, &MainWindow::onCloseArchive);

    m_toolbar->addAction(m_icons->find, tr("Find"), this, [this]() {
        m_searchBox->setVisible(!m_searchBox->isVisible());
        if (m_searchBox->isVisible()) m_searchBox->setFocus();
        else m_model->setFilterString(QString());
    });

    m_toolbar->addSeparator();

    m_toolbar->addAction(m_icons->info, tr("Info"), this, &MainWindow::onInfo);
}

void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    // Address bar
    auto addrLayout = new QHBoxLayout();
    auto addrLabel = new QLabel(tr("Address:"), this);
    m_addrBox = new QComboBox(this);
    m_addrBox->setEditable(true);
    m_upBtn = new QPushButton(
        style()->standardIcon(QStyle::SP_ArrowUp), QString(), this);
    m_upBtn->setToolTip(tr("Go to parent directory"));
    m_upBtn->setVisible(false);
    connect(m_upBtn, &QPushButton::clicked, this, [this]() {
        m_model->navigateUp();
        m_addrBox->setEditText(m_model->currentDir());
        m_upBtn->setVisible(!m_model->currentDir().isEmpty());
    });

    loadRecentFiles();
    connect(m_addrBox, QOverload<int>::of(&QComboBox::activated),
        this, [this](int index) {
            QString text = m_addrBox->itemText(index);
            if (QFileInfo::exists(text))
                openArchive(text);
        });

    addrLayout->addWidget(addrLabel);
    addrLayout->addWidget(m_addrBox, 1);
    addrLayout->addWidget(m_upBtn);
    m_mainLayout->addLayout(addrLayout);

    // Search bar
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText(tr("Search files..."));
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setVisible(false);
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_model->setFilterString(text);
    });
    m_mainLayout->addWidget(m_searchBox);

    // File tree
    m_model = new FileListModel(this);
    m_treeView = new ArchiveTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setAcceptDrops(false); // drops handled by MainWindow
    connect(m_treeView, &ArchiveTreeView::dragStarted, this, &MainWindow::onBeginDrag);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    m_treeView->setRootIsDecorated(false);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setEnabled(false);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setMinimumSectionSize(40);

    connect(m_treeView, &QTreeView::activated, this, &MainWindow::onItemDoubleClicked);
    connect(m_treeView, &ArchiveTreeView::backspacePressed, this, [this]() {
        if (!m_model->isFlatMode() && !m_model->currentDir().isEmpty()) {
            m_model->navigateUp();
            m_addrBox->setEditText(m_model->currentDir());
            m_upBtn->setVisible(!m_model->currentDir().isEmpty());
        }
    });
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_model, &FileListModel::directoryChanged, this, [this](const QString& dir) {
        m_addrBox->setEditText(dir);
        if (!m_model->isFlatMode())
            m_upBtn->setVisible(!dir.isEmpty());
    });
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, &MainWindow::updateStatusBar);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
        this, &MainWindow::updatePreview);

    // Column visibility toggle — right-click header
    m_treeView->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treeView->header(), &QHeaderView::customContextMenuRequested,
        this, [this](const QPoint& pos) {
            QMenu menu(this);
            for (int col = 1; col < FileListModel::ColCount; ++col)
            {
                QString name = m_model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
                if (name.isEmpty()) continue;
                QAction* act = menu.addAction(name);
                act->setCheckable(true);
                act->setChecked(!m_treeView->isColumnHidden(col));
                connect(act, &QAction::toggled, this, [this, col](bool shown) {
                    m_userManagedCols.insert(col);
                    m_treeView->setColumnHidden(col, !shown);
                });
            }
            menu.exec(m_treeView->header()->mapToGlobal(pos));
        });

    // Save column widths to QSettings when the user resizes them
    connect(m_treeView->header(), &QHeaderView::sectionResized,
        this, [this](int logicalIndex, int, int newSize) {
            QString colName = m_model->headerData(
                logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
            if (!colName.isEmpty())
                QSettings().setValue("columns/" + colName + "/width", newSize);
        });

    // ── Preview pane ────────────────────────────────────────────────────
    m_previewPanel = new PreviewPanel(this);
    m_previewPanel->hide();

    // ── Splitter ────────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_treeView);
    m_splitter->addWidget(m_previewPanel);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);

    m_mainLayout->addWidget(m_splitter, 1);

    {
        QSettings s;
        if (s.value("options/flatMode", false).toBool())
        {
            m_model->setFlatMode(true);
            if (m_flatAct) m_flatAct->setChecked(true);
            // up button hidden in flat mode
        }
    }

    setCentralWidget(m_centralWidget);
}

// ── Save/extract helpers moved to ArchiveProgress namespace ───────────

void MainWindow::updateStatusBar()
{
    if (!m_engine)
    {
        statusBar()->showMessage(tr("No archive open"));
        return;
    }

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty())
    {
        const auto& all = m_engine->ListContents();
        int fileCount = 0;
        uint64_t totalSize = 0, totalPacked = 0;
        for (const auto& e : all) {
            if (!e.isDirectory) {
                fileCount++;
                totalSize   += e.size;
                totalPacked += e.packedSize > 0 ? e.packedSize : e.size;
            }
        }
        QString fmt = QString::fromUtf8(m_engine->FormatName().data());
        int ratio = (totalSize > 0)
            ? static_cast<int>((1.0 - static_cast<double>(totalPacked) / totalSize) * 100)
            : 0;
        auto fmtB = [](uint64_t b) -> QString {
            if (b < 1024) return QString::number(b) + " B";
            if (b < 1024*1024) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
            if (b < static_cast<uint64_t>(1024)*1024*1024)
                return QString("%1 MB").arg(b / (1024.0*1024), 0, 'f', 2);
            return QString("%1 GB").arg(b / (1024.0*1024*1024), 0, 'f', 2);
        };
        statusBar()->showMessage(
            tr("%1  —  %2 files  —  %3 → %4  (%5% saved)")
                .arg(fmt)
                .arg(fileCount)
                .arg(fmtB(totalSize))
                .arg(fmtB(totalPacked))
                .arg(ratio));
        return;
    }

    auto paths = m_model->selectedEntryPaths(sel);
    const auto& entries = m_engine->ListContents();
    uint64_t totalSize = 0;
    int fileCount = 0;
    for (const auto& p : paths)
    {
        std::string sp = p.toStdString();
        for (const auto& e : entries)
        {
            if ((e.name == sp || e.path == sp) && !e.isDirectory)
            {
                totalSize += e.size;
                fileCount++;
                break;
            }
        }
    }

    QString sizeStr;
    if (totalSize < 1024)
        sizeStr = tr("%1 B").arg(totalSize);
    else if (totalSize < 1024 * 1024)
        sizeStr = tr("%1 KB").arg(totalSize / 1024.0, 0, 'f', 1);
    else if (totalSize < static_cast<uint64_t>(1024) * 1024 * 1024)
        sizeStr = tr("%1 MB").arg(totalSize / (1024.0 * 1024), 0, 'f', 1);
    else
        sizeStr = tr("%1 GB").arg(totalSize / (1024.0 * 1024 * 1024), 0, 'f', 2);

    statusBar()->showMessage(tr("%1 file(s) selected, %2").arg(fileCount).arg(sizeStr));
}

void MainWindow::afterActionDialog()
{
}

// Show the "After" dialog before a long job starts, return the chosen action.
static AfterAction AskAfterAction(QWidget* parent, const QString& title)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(QObject::tr("After the operation completes:")));
    auto* combo = new QComboBox();
    combo->addItems(GetAfterActionLabels());
    layout->addWidget(combo);
    auto* btn = new QPushButton(QObject::tr("Start"));
    QObject::connect(btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(btn);
    layout->addLayout(btnRow);
    if (dlg.exec() == QDialog::Accepted)
        return static_cast<AfterAction>(combo->currentIndex());
    return AfterAction::Nothing;
}

// ── Archive actions ────────────────────────────────────────────────────
void MainWindow::onNewArchive()
{
    CreateArchiveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto result = dlg.result();
    onCloseArchive();

    // Warn before overwriting an existing file
    if (QFileInfo::exists(result.path))
    {
        auto ret = QMessageBox::question(this, tr("Overwrite?"),
            tr("The file already exists:\n%1\n\nOverwrite?").arg(result.path),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    auto engine = ArchiveEngineFactory::CreateForFormat(result.format.toStdString());
    if (!engine || !engine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("Format not supported."));
        return;
    }

    // Apply Bit7zEngine settings if applicable
    auto* bit7z = dynamic_cast<Bit7zEngine*>(engine.get());
    if (bit7z)
    {
        if (!result.password.isEmpty())
            bit7z->setPassword(result.password.toStdString());
        if (result.encryptFilenames)
            bit7z->setEncryptHeaders(true);
        if (result.volumeSize > 0)
            bit7z->setVolumeSize(static_cast<uint64_t>(result.volumeSize) * 1024 * 1024);
        if (result.compressionMethod >= 0)
            bit7z->setCompressionMethod(result.compressionMethod);
        if (result.dictionarySize > 0)
            bit7z->setDictionarySize(result.dictionarySize);
        if (result.wordSize > 0)
            bit7z->setWordSize(result.wordSize);
        if (result.threadsCount > 0)
            bit7z->setThreadsCount(result.threadsCount);
        if (result.solidModeSet)
            bit7z->setSolidMode(result.solidMode);
    }

    if (!engine->Create(result.path.toStdString()))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not create archive."));
        return;
    }

    m_engine = std::move(engine);

    if (!result.comment.isEmpty())
        m_engine->setArchiveComment(result.comment.toStdString());

    // Auto-add source items if provided
    if (!result.sourcePaths.isEmpty())
    {
        // Show dialog immediately so user sees feedback during pre-count
        m_progressDlg = new QProgressDialog(tr("Counting files..."), nullptr,
            0, 0, this);
        m_progressDlg->setAutoClose(false);
        m_progressDlg->setAutoReset(false);
        m_progressDlg->setWindowModality(Qt::ApplicationModal);
        m_progressDlg->show();
        QApplication::processEvents();

        int total = 0;
        uint64_t totalBytes = 0;
        for (const auto& sp : result.sourcePaths)
        {
            fs::path src(sp.toStdWString());
            if (fs::is_directory(src))
            {
                for (const auto& de : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied))
                    if (de.is_regular_file()) { total++; totalBytes += fs::file_size(de); }
            }
            else if (fs::is_regular_file(src))
            {
                total++;
                totalBytes += fs::file_size(src);
            }
        }

        if (total == 0)
        {
            m_progressDlg->close();
            delete m_progressDlg;
            m_progressDlg = nullptr;
            return;
        }

        ProgressInfo pi;
        pi.start(totalBytes);

        m_progressDlg->setRange(0, total);
        m_progressDlg->setLabelText(tr("Adding files..."));

        int count = 0;
        for (const auto& sp : result.sourcePaths)
        {
            fs::path src(sp.toStdWString());
            if (fs::is_directory(src))
            {
                for (const auto& de : fs::recursive_directory_iterator(src))
                {
                    if (de.is_regular_file())
                    {
                        fs::path rel = de.path().lexically_relative(src);
                        m_engine->AddFile(de.path().string(), rel.generic_string());
                        m_progressDlg->setValue(++count);
                        pi.addBytes(fs::file_size(de));
                        if (pi.shouldUpdate())
                        {
                            pi.updateRate();
                            QString eta = pi.etaString();
                            QString label = QString::fromStdWString(rel.wstring())
                                + (eta.isEmpty() ? QString() : QChar('\n') + eta);
                            m_progressDlg->setLabelText(label);
                        }
                        QApplication::processEvents();
                    }
                }
            }
            else if (fs::is_regular_file(src))
            {
                m_engine->AddFile(src.string(), src.filename().string());
                m_progressDlg->setValue(++count);
                pi.addBytes(fs::file_size(src));
                if (pi.shouldUpdate())
                {
                    pi.updateRate();
                    QString eta = pi.etaString();
                    QString label = QString::fromStdWString(src.filename().wstring())
                        + (eta.isEmpty() ? QString() : QChar('\n') + eta);
                    m_progressDlg->setLabelText(label);
                }
                QApplication::processEvents();
            }
        }

        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = new QProgressDialog(tr("Saving..."), tr("Cancel"),
            0, 0, this);
        m_progressDlg->setAutoClose(false);
        m_progressDlg->setAutoReset(false);
        m_progressDlg->setWindowModality(Qt::ApplicationModal);

        // Embed "After" combo directly in the progress dialog
        auto* afterRow = new QHBoxLayout();
        afterRow->addWidget(new QLabel(tr("After:")));
        auto* afterCombo = new QComboBox();
        afterCombo->addItems(GetAfterActionLabels());
        afterRow->addWidget(afterCombo);
        auto* dl = m_progressDlg->layout();
        if (dl) dl->addItem(afterRow);

        m_progressDlg->show();

        auto sr = ArchiveProgress::save(m_engine.get(), m_progressDlg, this);
        m_progressDlg = nullptr;
        if (sr != ArchiveProgress::SaveResult::Ok)
        {
            refreshFileList();
            return;
        }

        AfterAction afterAction = static_cast<AfterAction>(afterCombo->currentIndex());
        if (afterAction != AfterAction::Nothing)
            ExecuteAfterAction(afterAction);
    }

    m_currentPath = result.path.toStdString();
    addRecentFile(result.path);
    m_treeView->setEnabled(true);
    statusBar()->showMessage(tr("Archive created"), 3000);
    refreshFileList();
}

void MainWindow::onOpenArchive()
{
    auto extList = ArchiveEngineFactory::SupportedExtensions();
    QString filter = "Supported Archives (";
    for (const auto& e : extList)
        filter += "*" + QString::fromStdString(e) + " ";
    filter.chop(1);
    filter += ");;All (*.*)";

    QString path = QFileDialog::getOpenFileName(this, tr("Open Archive"), "", filter);
    if (path.isEmpty()) return;

    openArchive(path);
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings settings;
    QStringList recent;
    int size = settings.beginReadArray("recentFiles");
    for (int i = 0; i < size && recent.size() < 9; ++i)
    {
        settings.setArrayIndex(i);
        QString p = settings.value("path").toString();
        if (p != path && QFileInfo::exists(p))
            recent.append(p);
    }
    settings.endArray();
    recent.prepend(path);

    settings.beginWriteArray("recentFiles");
    for (int i = 0; i < recent.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("path", recent[i]);
    }
    settings.endArray();

    // Update combo box without triggering the activated signal
    m_addrBox->blockSignals(true);
    m_addrBox->clear();
    for (const auto& p : recent)
        m_addrBox->addItem(p);
    m_addrBox->setEditText(path);
    m_addrBox->blockSignals(false);
}

void MainWindow::loadRecentFiles()
{
    QSettings settings;
    int size = settings.beginReadArray("recentFiles");
    m_addrBox->clear();
    m_addrBox->setEditText(QString());
    int loaded = 0;
    for (int i = 0; i < size && loaded < 10; ++i)
    {
        settings.setArrayIndex(i);
        QString path = settings.value("path").toString();
        if (QFileInfo::exists(path))
        {
            m_addrBox->addItem(path);
            loaded++;
        }
    }
    settings.endArray();
    // Trim excess entries from QSettings
    if (size > 10)
    {
        QStringList keep;
        for (int i = 0; i < m_addrBox->count(); ++i)
            keep.append(m_addrBox->itemText(i));
        settings.beginWriteArray("recentFiles");
        for (int i = 0; i < keep.size(); ++i)
        {
            settings.setArrayIndex(i);
            settings.setValue("path", keep[i]);
        }
        settings.endArray();
    }
    m_addrBox->setEditText(QString());
    m_addrBox->setCurrentIndex(-1);
}

bool MainWindow::openArchive(const QString& path)
{
    onCloseArchive();

    std::string spath = path.toStdString();
    std::string firstVolPath = ArchiveEngineFactory::ResolveFirstVolume(spath);

    auto engine = ArchiveEngineFactory::CreateForFile(firstVolPath);
    if (!engine)
    {
        QMessageBox::warning(this, tr("Open Failed"),
            tr("Could not open the archive."));
        return false;
    }

    engine->resetOpenCancel();

    // Progress dialog for opening large archives
    struct { std::mutex m; ArchiveEngine::OpenProgressInfo info; } op;

    engine->setOpenProgressCb([&](const ArchiveEngine::OpenProgressInfo& info) {
        std::lock_guard<std::mutex> lock(op.m);
        op.info = info;
    });

    std::atomic<bool> openDone{false};
    std::atomic<bool> openOk{false};

    std::thread openThread([&]() {
        openOk = engine->Open(firstVolPath);
        openDone = true;
    });

    m_progressDlg = new QProgressDialog(tr("Opening archive..."), tr("Cancel"), 0, 0, this);
    m_progressDlg->setWindowModality(Qt::ApplicationModal);
    m_progressDlg->setAutoClose(false);
    m_progressDlg->setAutoReset(false);
    m_progressDlg->show();

    QString fileName = QFileInfo(path).fileName();

    while (!openDone)
    {
        QApplication::processEvents();
        QThread::msleep(16);

        if (m_progressDlg && m_progressDlg->wasCanceled())
        {
            engine->cancelOpen();
            openOk = false;
            openDone = true;
            break;
        }

        int64_t currentBytes = 0;
        int64_t totalBytes = 0;
        {
            std::lock_guard<std::mutex> lock(op.m);
            currentBytes = op.info.currentBytes;
            totalBytes = op.info.totalBytes;
        }

        if (totalBytes > 0)
        {
            m_progressDlg->setRange(0, totalBytes);
            m_progressDlg->setValue(currentBytes);
            int pct = (totalBytes > 0) ? static_cast<int>((currentBytes * 100) / totalBytes) : 0;
            m_progressDlg->setLabelText(
                fileName + QChar('\n') + tr("%1%").arg(pct));
        }
        else
        {
            // Indeterminate progress
            m_progressDlg->setLabelText(
                fileName + QChar('\n') + tr("Reading entries... %1").arg(currentBytes));
        }
    }

    if (openThread.joinable())
        openThread.join();

    engine->setOpenProgressCb(nullptr);

    if (m_progressDlg)
    {
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
    }

    // If the user cancelled, don't show an error message
    if (!openOk && !engine->isOpenCancelled())
    {
        QMessageBox::warning(this, tr("Open Failed"),
            tr("Could not open the archive."));
        return false;
    }
    else if (engine->isOpenCancelled())
    {
        return false;
    }

    m_engine = std::move(engine);
    // Auto-load saved password if not already set by the user this session
    if (m_archivePassword.empty())
    {
        QString saved = loadPassword(QFileInfo(QString::fromStdString(firstVolPath)).absoluteFilePath());
        if (!saved.isEmpty())
            m_archivePassword = saved.toStdString();
    }
    if (!m_archivePassword.empty())
        m_engine->setPassword(m_archivePassword);
    m_currentPath = firstVolPath;
    addRecentFile(QString::fromStdString(firstVolPath));
    m_treeView->setEnabled(true);
    refreshFileList();
    statusBar()->showMessage(tr("Opened: %1").arg(QString::fromStdString(firstVolPath)), 3000);
    return true;
}

void MainWindow::onCloseArchive()
{
    if (m_engine)
    {
        m_engine->Close();
        m_engine.reset();
    }
    m_currentPath.clear();
    m_model->clear();
    m_addrBox->clearEditText();
    m_treeView->setEnabled(false);
    m_archiveCommentAct->setEnabled(false);
    m_setPasswordAct->setEnabled(false);
    setWindowTitle(tr("ZipFX %1").arg(ZIPFX_VERSION));
    statusBar()->showMessage(tr("No archive open"));
}

void MainWindow::onAddFiles()
{
    if (!m_engine || !m_engine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("No archive open or read-only."));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Files"));
    if (!files.isEmpty())
        doAddPaths(files);
}

void MainWindow::onAddFolder()
{
    if (!m_engine || !m_engine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("No archive open or read-only."));
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
    if (!dir.isEmpty())
        doAddPaths({dir});
}

void MainWindow::doAddPaths(const QStringList& paths)
{
    if (paths.isEmpty()) return;

    auto isExcluded = [this](const QString& filename) -> bool {
        for (const auto& pat : m_excludePatterns)
            if (QDir::match(pat.trimmed(), filename))
                return true;
        return false;
    };

    QString prefix = m_model->currentDir();
    if (!prefix.isEmpty()) prefix += "/";

    // Show dialog immediately so user sees feedback during pre-count
    m_progressDlg = new QProgressDialog(tr("Counting files..."), tr("Cancel"),
        0, 0, this);
    m_progressDlg->setAutoClose(false);
    m_progressDlg->setAutoReset(false);
    m_progressDlg->setWindowModality(Qt::ApplicationModal);
    m_progressDlg->show();
    QApplication::processEvents();

    // Count total files & compute total bytes (recursing directories)
    int total = 0;
    uint64_t totalBytes = 0;
    for (const auto& path : paths)
    {
        fs::path src(path.toStdWString());
        if (fs::is_directory(src))
        {
            for (const auto& de : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied))
                if (de.is_regular_file()) {
                    QString fname = QString::fromStdWString(de.path().filename().wstring());
                    if (!isExcluded(fname)) { total++; totalBytes += fs::file_size(de); }
                }
        }
        else if (fs::is_regular_file(src))
        {
            total++;
            totalBytes += fs::file_size(src);
        }
    }

    if (total == 0)
    {
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
        return;
    }

    ProgressInfo pi;
    pi.start(totalBytes);

    m_progressDlg->setRange(0, total);
    m_progressDlg->setLabelText(tr("Adding files..."));

    int count = 0;
    for (const auto& path : paths)
    {
        if (m_progressDlg->wasCanceled()) break;

        fs::path src(path.toStdWString());
        QString archiveBase = prefix + QString::fromStdWString(src.filename().wstring());

        if (fs::is_directory(src))
        {
            for (const auto& de : fs::recursive_directory_iterator(src))
            {
                if (m_progressDlg->wasCanceled()) break;
                if (de.is_regular_file())
                {
                    QString fname0 = QString::fromStdWString(de.path().filename().wstring());
                    if (isExcluded(fname0)) continue;
                    fs::path rel = de.path().lexically_relative(src);
                    QString archivePath = archiveBase + "/"
                        + QString::fromStdWString(rel.generic_wstring());
                    m_engine->AddFile(de.path().string(), archivePath.toStdString());
                    m_progressDlg->setValue(++count);
                    pi.addBytes(fs::file_size(de));
                    {
                        QString fname = QString::fromStdWString(rel.wstring());
                        if (pi.shouldUpdate())
                        {
                            pi.updateRate();
                            QString eta = pi.etaString();
                            fname += (eta.isEmpty() ? QString() : QChar('\n') + eta);
                        }
                        m_progressDlg->setLabelText(fname);
                    }
                    QApplication::processEvents();
                }
            }
        }
        else if (fs::is_regular_file(src))
        {
            if (isExcluded(QString::fromStdWString(src.filename().wstring()))) continue;
            m_engine->AddFile(path.toStdString(), archiveBase.toStdString());
            m_progressDlg->setValue(++count);
            pi.addBytes(fs::file_size(src));
            {
                QString fname = QString::fromStdWString(src.filename().wstring());
                if (pi.shouldUpdate())
                {
                    pi.updateRate();
                    QString eta = pi.etaString();
                    fname += (eta.isEmpty() ? QString() : QChar('\n') + eta);
                }
                m_progressDlg->setLabelText(fname);
            }
            QApplication::processEvents();
        }
    }

    if (m_progressDlg->wasCanceled())
    {
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
        std::string path = m_currentPath;
        m_engine->Close();
        m_engine->Open(path);
        refreshFileList();
        statusBar()->showMessage(tr("Add cancelled."), 3000);
        return;
    }

    // Embed "After" combo directly in the progress dialog (same
    // dialog used by the counting/adding phase).
    auto* afterRow = new QHBoxLayout();
    afterRow->addWidget(new QLabel(tr("After:")));
    auto* afterCombo = new QComboBox();
    afterCombo->addItems(GetAfterActionLabels());
    afterRow->addWidget(afterCombo);
    auto* dl = m_progressDlg->layout();
    if (dl) dl->addItem(afterRow);

    // Switch the dialog from "Adding files..." to "Saving..." (but keep the dialog open)
    m_progressDlg->setRange(0, 0);
    m_progressDlg->setLabelText(tr("Saving..."));

    auto sr = ArchiveProgress::save(m_engine.get(), m_progressDlg, this);
    m_progressDlg = nullptr;
    if (sr != ArchiveProgress::SaveResult::Ok)
    {
        refreshFileList();
        return;
    }

    AfterAction afterAction = static_cast<AfterAction>(afterCombo->currentIndex());
    if (afterAction != AfterAction::Nothing)
        ExecuteAfterAction(afterAction);
    refreshFileList();
}

void MainWindow::onExtractAll()
{
    if (!m_engine) return;

    QString startDir = m_currentPath.empty()
        ? QString()
        : QFileInfo(QString::fromStdString(m_currentPath)).absolutePath();
    QString dest = QFileDialog::getExistingDirectory(this, tr("Extract to"), startDir);
    if (dest.isEmpty()) return;

    doExtract(dest, true);
}

void MainWindow::onExtractSelected()
{
    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty())
    {
        QMessageBox::information(this, tr("Info"), tr("Select files first."));
        return;
    }

    doExtractSelected(sel);
}

// Returns a specific reason to append to a generic extraction-failure
// message, or an empty string when there's nothing more precise to say
// (e.g. the entry isn't flagged encrypted, or the active engine can
// actually decrypt but the password was simply wrong).
QString MainWindow::extractionFailureDetail(const ArchiveEntry& entry) const
{
    if (!entry.isEncrypted || !m_engine) return {};
    std::string reason = m_engine->EncryptionUnavailableReason();
    if (!reason.empty())
        return QString::fromStdString(reason);
    return tr("This entry is encrypted — check that the password is correct.");
}

void MainWindow::doExtract(const QString& destPath, bool all, bool stripPaths)
{
    auto entries = m_engine->ListContents();
    if (entries.empty())
    {
        QMessageBox::information(this, tr("Info"), tr("Archive is empty."));
        return;
    }

    // Filter entries if only selected
    std::vector<ArchiveEntry> toExtract;
    if (!all)
    {
        auto sel = m_treeView->selectionModel()->selectedRows(0);
        auto paths = m_model->selectedEntryPaths(sel);
        for (const auto& p : paths)
        {
            std::string sp = p.toStdString();
            std::string prefix = sp + "/";
            for (const auto& e : entries)
            {
                if (e.path == sp || e.path == prefix
                    || e.path.rfind(prefix, 0) == 0)
                {
                    toExtract.push_back(e);
                }
            }
        }
    }
    else
    {
        toExtract = entries;
    }

    m_extractCancelled = false;

    uint64_t totalBytes = 0;
    for (const auto& e : toExtract)
        totalBytes += e.size;

    ProgressInfo pi;
    pi.start(totalBytes);

    m_progressDlg = new QProgressDialog(tr("Extracting..."), tr("Cancel"),
        0, 100, this);
    m_progressDlg->setAutoClose(false);
    m_progressDlg->setAutoReset(false);
    m_progressDlg->setWindowModality(Qt::ApplicationModal);
    if (auto* lbl = m_progressDlg->findChild<QLabel*>())
        lbl->setAlignment(Qt::AlignLeft);
    m_progressDlg->show();

    bool applyToAll = false;
    uint64_t bytesSoFar = 0;
    QStringList failures;

    for (size_t i = 0; i < toExtract.size(); ++i)
    {
        if (m_progressDlg->wasCanceled())
        {
            m_extractCancelled = true;
            break;
        }

        const auto& entry = toExtract[i];
        if (!ArchiveEngine::isSafeEntryName(entry.path)) {
            qWarning("Skipping unsafe entry: %s", entry.path.c_str());
            continue;
        }
        QString name = QString::fromUtf8(entry.name.c_str());

        QString entryRelPath = stripPaths
            ? QFileInfo(QString::fromUtf8(entry.path.c_str())).fileName()
            : QString::fromUtf8(entry.path.c_str());
        QString destFile = destPath + "/" + entryRelPath;

        if (QFileInfo::exists(destFile) && !applyToAll)
        {
            auto ret = QMessageBox::question(this, tr("Overwrite?"),
                tr("File exists:\n%1\nOverwrite?").arg(name),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (ret == QMessageBox::Cancel) break;
            if (ret == QMessageBox::No) continue;
        }

        if (entry.isDirectory)
        {
            if (!stripPaths) QDir().mkpath(destFile);
            continue;
        }

        QDir().mkpath(QFileInfo(destFile).path());

        bool extractOk = ArchiveProgress::extractFile(m_engine.get(), entry, destFile, pi, bytesSoFar, m_progressDlg);

        bytesSoFar += entry.size;
        pi.bytesProcessed = bytesSoFar;

        if (!extractOk)
        {
            if (m_extractCancelled)
            {
                statusBar()->showMessage(tr("Extraction cancelled."), 3000);
                break;
            }
            if (!m_keepBrokenFiles && QFileInfo::exists(destFile))
                QFile::remove(destFile);
            qWarning("Failed to extract: %s", entry.path.c_str());
            QString detail = extractionFailureDetail(entry);
            failures << (detail.isEmpty() ? name : tr("%1 — %2").arg(name, detail));
        }
    }

    m_progressDlg->close();
    delete m_progressDlg;
    m_progressDlg = nullptr;

    if (!failures.isEmpty() && !m_extractCancelled)
    {
        QString msg = failures.size() == 1
            ? tr("1 file could not be extracted:\n\n%1").arg(failures.first())
            : tr("%1 files could not be extracted:\n\n%2")
                  .arg(failures.size())
                  .arg(failures.mid(0, 10).join('\n')
                       + (failures.size() > 10 ? tr("\n… and %1 more").arg(failures.size() - 10) : QString()));
        QMessageBox::warning(this, tr("Extraction Incomplete"), msg);
    }
    else
    {
        statusBar()->showMessage(tr("Extraction complete"), 3000);
    }

    if (m_openAfterExtract && !m_extractCancelled)
        QDesktopServices::openUrl(QUrl::fromLocalFile(destPath));
}

void MainWindow::doExtractSelected(const QModelIndexList& selection)
{
    (void)selection;
    QString dest = QFileDialog::getExistingDirectory(this, tr("Extract selected to"));
    if (dest.isEmpty()) return;

    doExtract(dest, false);
}

void MainWindow::onTest()
{
    if (!m_engine) return;

    auto entries = m_engine->ListContents();
    if (entries.empty())
    {
        QMessageBox::information(this, tr("Test"), tr("Archive is empty."));
        return;
    }

    int total = (int)entries.size();

    uint64_t totalBytes = 0;
    for (const auto& e : entries)
        totalBytes += e.packedSize > 0 ? e.packedSize : e.size;

    ProgressInfo pi;
    pi.start(totalBytes);

    QProgressDialog prog(tr("Testing integrity..."), tr("Cancel"),
        0, total, this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.show();

    bool result = m_engine->TestIntegrity(
        [&](int current, int) {
            prog.setValue(current);
            if (current < total)
            {
                pi.addBytes(entries[current].packedSize > 0
                    ? entries[current].packedSize : entries[current].size);
                if (pi.shouldUpdate())
                {
                    pi.updateRate();
                    prog.setLabelText(tr("Testing: %1 %2").arg(
                        QString::fromUtf8(entries[current].name.c_str()), pi.etaString()));
                }
                else
                {
                    prog.setLabelText(tr("Testing: %1").arg(
                        QString::fromUtf8(entries[current].name.c_str())));
                }
            }
            QApplication::processEvents();
        },
        [&]() -> bool { return prog.wasCanceled(); }
    );

    prog.close();

    if (result)
        QMessageBox::information(this, tr("Test"), tr("Integrity check passed."));
    else if (prog.wasCanceled())
        statusBar()->showMessage(tr("Integrity check cancelled."), 3000);
    else
        QMessageBox::warning(this, tr("Test"), tr("Integrity check FAILED."));
}

void MainWindow::onView()
{
    if (!m_engine) return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths({sel[0]});
    if (paths.isEmpty()) return;

    QString name = paths[0];
    QString ext = QFileInfo(name).suffix().toLower();

    // Look up actual size from the cached entry list (avoids reading the file for size)
    uint64_t entrySize = 0;
    std::string entryPath = name.toStdString();
    for (const auto& e : m_engine->ListContents())
    {
        if (e.name == entryPath || e.path == entryPath)
        {
            entrySize = e.size;
            break;
        }
    }

    // Bail out for formats we can't preview without reading anything
    if (ext == "pdf" || ext == "doc" || ext == "docx" || ext == "xls" || ext == "xlsx")
    {
        QMessageBox::information(this, tr("Info"),
            tr("File: %1\nSize: %2 bytes\n\nCannot preview this format.").arg(name).arg(entrySize));
        return;
    }

    // Check engine-level view support (e.g., solid or multi-volume archives)
    if (!m_engine->SupportsViewFile())
    {
        QString reason = QString::fromStdString(m_engine->ViewUnsupportedReason());
        QString msg = reason.isEmpty()
            ? tr("Viewing individual files is not supported for this archive.")
            : tr("Viewing individual files is not supported for this archive (%1).").arg(reason);
        msg += tr("\n\nExtract the file first, then open it.");
        QMessageBox::information(this, tr("View"), msg);
        return;
    }

    static const QStringList imgExts = {"png", "jpg", "jpeg", "gif", "bmp", "svg", "webp", "ico", "tiff", "tif"};
    static constexpr size_t kTextLimit = 100000;
    static constexpr size_t kHexLimit  = 4096;

    // For non-images only read as much as we can display
    std::vector<uint8_t> data;
    if (imgExts.contains(ext))
        data = m_engine->ReadFile(entryPath);
    else
        data = m_engine->ReadFilePartial(entryPath, kTextLimit);

    if (data.empty())
    {
        if (entrySize == 0)
        {
            QMessageBox::information(this, tr("View"), tr("The file \"%1\" is empty.").arg(name));
        }
        else
        {
            QMessageBox::warning(this, tr("View"),
                tr("Could not read \"%1\".\n\n"
                   "The archive may be encrypted, corrupted, or use a compression "
                   "method that is not supported for in-place preview.\n\n"
                   "Try extracting the file first.").arg(name));
        }
        return;
    }

    // For images use the actual loaded size; for others use entry metadata
    uint64_t displaySize = imgExts.contains(ext) ? static_cast<uint64_t>(data.size()) : entrySize;

    auto dlg = new QDialog(this);
    dlg->setWindowTitle(tr("View: %1").arg(name));
    auto* layout = new QVBoxLayout(dlg);

    auto* infoLabel = new QLabel(tr("%1  —  %2 bytes").arg(name).arg(displaySize), dlg);
    layout->addWidget(infoLabel);

    std::function<void()> fitImageFn;

    if (imgExts.contains(ext))
    {
        QPixmap pix;
        if (pix.loadFromData(data.data(), static_cast<uint32_t>(data.size())))
        {
            auto* scene = new QGraphicsScene(dlg);
            auto* pixItem = scene->addPixmap(pix);
            auto* view = new QGraphicsView(scene, dlg);
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->setFrameShape(QFrame::NoFrame);
            layout->addWidget(view, 1);

            fitImageFn = [view, pixItem]() {
                view->fitInView(pixItem->sceneBoundingRect(), Qt::KeepAspectRatio);
            };
            // Don't fit here — the dialog is still its default/uninitialized
            // size at this point (resize()/show() haven't run yet below), so
            // fitInView() would scale against the wrong viewport and the
            // image would render tiny. The initial fit happens after show()
            // once geometry is final; the event filter refits on later
            // live resizes.
            dlg->installEventFilter(new ResizeEventFilter(fitImageFn, dlg));
        }
        else
        {
            auto* text = new QTextEdit(dlg);
            text->setReadOnly(true);
            text->setPlainText(tr("Failed to load image."));
            layout->addWidget(text);
        }
    }
    else
    {
        // Detect binary vs text by checking for null/control bytes in the first 4096 bytes
        bool isText = !data.empty();
        for (size_t i = 0; i < data.size() && i < kHexLimit; ++i)
        {
            if (data[i] == 0 || (data[i] < 0x09 && data[i] != 0x0A && data[i] != 0x0D))
            {
                isText = false;
                break;
            }
        }

        auto* text = new QTextEdit(dlg);
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        if (isText)
        {
            const char* raw = reinterpret_cast<const char*>(data.data());
            int rawLen = static_cast<int>(data.size());
            QString content = QString::fromUtf8(raw, rawLen);
            // If decoding produced replacement characters, fall back to local encoding
            if (content.contains(QChar::ReplacementCharacter))
                content = QString::fromLocal8Bit(raw, rawLen);
            if (entrySize > kTextLimit)
                content += tr("\n\n... truncated (showing first %1 of %2 bytes)")
                               .arg(kTextLimit).arg(entrySize);
            text->setPlainText(content);
        }
        else
        {
            // Hex dump — we only have up to kTextLimit bytes but only show kHexLimit
            QString hex;
            size_t maxBytes = std::min(data.size(), kHexLimit);
            for (size_t i = 0; i < maxBytes; i += 16)
            {
                hex += QString("%1  ").arg(i, 8, 16, QChar('0'));
                for (size_t j = 0; j < 16; ++j)
                {
                    if (i + j < maxBytes)
                        hex += QString("%1 ").arg(data[i + j], 2, 16, QChar('0'));
                    else
                        hex += "   ";
                    if (j == 7) hex += " ";
                }
                hex += " ";
                for (size_t j = 0; j < 16 && i + j < maxBytes; ++j)
                {
                    char c = static_cast<char>(data[i + j]);
                    hex += (c >= 32 && c < 127) ? c : '.';
                }
                hex += "\n";
            }
            if (entrySize > kHexLimit)
                hex += tr("... truncated (showing first %1 of %2 bytes)")
                           .arg(kHexLimit).arg(entrySize);
            text->setPlainText(hex);
        }
        layout->addWidget(text, 1);
    }

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), dlg);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    {
        QSettings s;
        QByteArray geo = s.value("viewDialog/geometry").toByteArray();
        if (!geo.isEmpty()) dlg->restoreGeometry(geo);
        else dlg->resize(700, 500);
    }
    connect(dlg, &QDialog::finished, this, [dlg]() {
        QSettings().setValue("viewDialog/geometry", dlg->saveGeometry());
    });
    dlg->show();
    if (fitImageFn)
        QTimer::singleShot(0, dlg, fitImageFn);
}

void MainWindow::onInfo()
{
    if (!m_engine) return;

    auto entries = m_engine->ListContents();

    uint64_t totalSize = 0, totalPacked = 0;
    int files = 0, folders = 0;
    std::set<std::string> methods;
    for (const auto& e : entries)
    {
        if (e.isDirectory) { folders++; continue; }
        files++;
        totalSize += e.size;
        totalPacked += e.packedSize > 0 ? e.packedSize : e.size;
        if (!e.compressionMethod.empty())
            methods.insert(e.compressionMethod);
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Archive Information"));
    dlg.setMinimumWidth(480);
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    lay->addLayout(form);

    auto addRow = [&](const QString& label, const QString& value) {
        auto* lbl = new QLabel(value, &dlg);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lbl->setWordWrap(true);
        form->addRow(label, lbl);
    };

    addRow(tr("Archive:"), QString::fromStdString(m_currentPath));
    addRow(tr("Format:"), QString::fromUtf8(m_engine->FormatName().data()));

    if (!methods.empty())
    {
        QStringList ml;
        for (const auto& m : methods) ml << QString::fromStdString(m);
        addRow(tr("Method:"), ml.join(", "));
    }

    addRow(tr("Files:"), QString::number(files));
    addRow(tr("Folders:"), QString::number(folders));

    auto fmtBytes = [](uint64_t b) -> QString {
        if (b < 1024) return QString("%1 B").arg(b);
        if (b < 1024*1024) return QString("%1 KB (%2 B)").arg(b/1024.0, 0, 'f', 1).arg(b);
        if (b < static_cast<uint64_t>(1024)*1024*1024)
            return QString("%1 MB (%2 B)").arg(b/(1024.0*1024), 0, 'f', 2).arg(b);
        return QString("%1 GB (%2 B)").arg(b/(1024.0*1024*1024), 0, 'f', 3).arg(b);
    };

    addRow(tr("Uncompressed:"), fmtBytes(totalSize));
    addRow(tr("Compressed:"), fmtBytes(totalPacked));

    if (totalSize > 0)
    {
        int saved = static_cast<int>((1.0 - static_cast<double>(totalPacked) / totalSize) * 100);
        addRow(tr("Ratio:"), tr("%1% saved").arg(saved));
    }

    std::string comment = m_engine->archiveComment();
    if (!comment.empty())
        addRow(tr("Comment:"), QString::fromUtf8(comment.c_str()));

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    dlg.exec();
}

void MainWindow::onArchiveComment()
{
    if (!m_engine || !m_engine->supportsArchiveComment()) return;

    std::string current = m_engine->archiveComment();
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this, tr("Archive Comment"),
        tr("Comment:"), QString::fromUtf8(current.c_str()), &ok);
    if (!ok || text.toStdString() == current) return;

    if (m_engine->setArchiveComment(text.toStdString()))
    {
        ArchiveProgress::runSave(m_engine.get(), tr("Updating comment..."), this);
        statusBar()->showMessage(tr("Archive comment updated."), 3000);
    }
}

void MainWindow::onEntryComment()
{
    if (!m_engine) return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths(sel);
    if (paths.isEmpty()) return;

    QString entryPath = paths[0];
    std::string entryStd = entryPath.toStdString();

    // Find current comment
    std::string current;
    for (const auto& e : m_engine->ListContents())
        if (e.path == entryStd) { current = e.comment; break; }

    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this, tr("File Comment"),
        tr("Comment for %1:").arg(entryPath),
        QString::fromUtf8(current.c_str()), &ok);
    if (!ok) return;

    if (m_engine->setEntryComment(entryStd, text.toStdString()))
    {
        ArchiveProgress::runSave(m_engine.get(), tr("Updating comment..."), this);
        refreshFileList();
        statusBar()->showMessage(tr("File comment updated."), 3000);
    }
    else
    {
        QMessageBox::warning(this, tr("Error"),
            tr("This format does not support file comments."));
    }
}

void MainWindow::onDelete()
{
    if (!m_engine || !m_engine->SupportsCreation()) return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths(sel);
    if (paths.isEmpty()) return;

    if (QMessageBox::question(this, tr("Confirm"),
            tr("Delete %1 files?").arg(paths.size())) != QMessageBox::Yes)
        return;

    for (const auto& p : paths)
        m_engine->RemoveEntry(p.toStdString());

    ArchiveProgress::runSave(m_engine.get(), tr("Deleting..."), this);
    refreshFileList();
}

// ── Open entry with external application ──────────────────────────────
void MainWindow::onOpenEntry(bool pickApp)
{
    if (!m_engine) return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths({sel[0]});
    if (paths.isEmpty()) return;

    QString entryPath = paths[0];
    QFileInfo fi(entryPath);

    // Look up the archive entry; skip directories
    std::string key = entryPath.toStdString();
    const ArchiveEntry* foundEntry = nullptr;
    for (const auto& e : m_engine->ListContents())
    {
        if ((e.name == key || e.path == key) && !e.isDirectory)
        {
            foundEntry = &e;
            break;
        }
    }
    if (!foundEntry) return;

    QString filename = fi.fileName();
    // QTemporaryDir uses an atomic mkdtemp-equivalent creation (no separate
    // compute-path-then-mkpath TOCTOU window) with restrictive permissions.
    QTemporaryDir tempDirObj(QDir::tempPath() + "/ZipFX_Open_XXXXXX");
    if (!tempDirObj.isValid()) return;
    tempDirObj.setAutoRemove(false);
    QString tempDir = tempDirObj.path() + "/";
    TempCleanup::registerPath(tempDir.toStdString());
    QString destFile = tempDir + filename;

    if (!pickApp)
    {
        // "Open" — extract with a proper progress dialog
        m_extractCancelled = false;
        m_progressDlg = new QProgressDialog(
            tr("Opening \"%1\"\u2026").arg(filename),
            tr("Cancel"), 0, 100, this);
        m_progressDlg->setAutoClose(false);
        m_progressDlg->setAutoReset(false);
        m_progressDlg->setWindowModality(Qt::ApplicationModal);
        if (auto* lbl = m_progressDlg->findChild<QLabel*>())
            lbl->setAlignment(Qt::AlignLeft);
        m_progressDlg->show();

        ProgressInfo pi;
        pi.start(foundEntry->size);

        bool ok = ArchiveProgress::extractFile(m_engine.get(), *foundEntry, destFile, pi, 0, m_progressDlg);

        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;

        if (!ok)
        {
            if (!m_extractCancelled)
            {
                QString detail = extractionFailureDetail(*foundEntry);
                QString msg = detail.isEmpty()
                    ? tr("Could not extract \"%1\".").arg(filename)
                    : tr("Could not extract \"%1\".\n\n%2").arg(filename, detail);
                QMessageBox::warning(this, tr("Error"), msg);
            }
            return;
        }

        QDesktopServices::openUrl(QUrl::fromLocalFile(destFile));
        return;
    }

    // "Open with..." — existing wait-cursor behaviour
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_engine->Extract(key, destFile.toStdString());
    QApplication::restoreOverrideCursor();

    if (!ok)
    {
        QString detail = extractionFailureDetail(*foundEntry);
        QString msg = detail.isEmpty()
            ? tr("Could not extract \"%1\".").arg(filename)
            : tr("Could not extract \"%1\".\n\n%2").arg(filename, detail);
        QMessageBox::warning(this, tr("Error"), msg);
        return;
    }

#ifdef _WIN32
    ShellExecuteW(nullptr, L"openas",
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(destFile).utf16()),
        nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-W", destFile});
#else
    QString app = QFileDialog::getOpenFileName(this, tr("Open With..."),
        QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation).value(0, "/usr/bin"));
    if (!app.isEmpty())
        QProcess::startDetached(app, {destFile});
#endif
}

// ── List interactions ──────────────────────────────────────────────────
void MainWindow::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* model = static_cast<FileListModel*>(m_treeView->model());

    auto* item = static_cast<FileListModel::Item*>(index.internalPointer());
    if (!item) return;

    if (item->name == ".." && !model->isFlatMode())
    {
        model->navigateUp();
    }
    else if (item->isDir && !model->isFlatMode())
    {
        model->navigateInto(item->name);
    }
    else if (!item->isDir && !item->isParent)
    {
        onView();
    }
}

void MainWindow::onContextMenu(const QPoint& pos)
{
    bool hasEngine    = (m_engine != nullptr);
    bool hasSelection = !m_treeView->selectionModel()->selectedRows(0).isEmpty();

    // Determine if selection contains only directories (for disabling file-only actions)
    bool selectionIsDir = false;
    if (hasEngine && hasSelection)
    {
        auto sel = m_treeView->selectionModel()->selectedRows(0);
        auto paths = m_model->selectedEntryPaths(sel);
        selectionIsDir = !paths.isEmpty();
        for (const auto& p : paths)
        {
            std::string sp = p.toStdString();
            for (const auto& e : m_engine->ListContents())
            {
                if (e.name == sp || e.path == sp)
                {
                    if (!e.isDirectory) { selectionIsDir = false; break; }
                    break;
                }
            }
            if (!selectionIsDir) break;
        }
    }

    QMenu menu(this);

    // Extract Here (fast path — no dialog)
    if (hasEngine)
    {
        QAction* extractHereAct = menu.addAction(tr("Extract Here"), this, [this]() {
            if (!m_engine) return;
            QFileInfo fi(QString::fromStdString(m_currentPath));
            QString archiveName = fi.completeBaseName();
            if (archiveName.endsWith(".tar", Qt::CaseInsensitive))
                archiveName = QFileInfo(archiveName).completeBaseName();
            QString destPath = fi.dir().filePath(archiveName);
            QDir().mkpath(destPath);
            doExtract(destPath, true, false);
        });
        extractHereAct->setEnabled(hasEngine);
    }

    QAction* extractAct = menu.addAction(tr("Extract..."), this, [this, hasSelection]() {
        auto sel = m_treeView->selectionModel()->selectedRows(0);
        if (hasSelection)
            doExtractSelected(sel);
        else
            onExtractAll();
    });
    extractAct->setEnabled(hasEngine);

    menu.addSeparator();

    QAction* viewAct = menu.addAction(tr("View"), this, &MainWindow::onView);
    viewAct->setEnabled(hasEngine && hasSelection && !selectionIsDir);

    QAction* openAct = menu.addAction(tr("Open"), this, [this]() { onOpenEntry(false); });
    openAct->setEnabled(hasEngine && hasSelection && !selectionIsDir);

    if (hasSelection)
    {
        menu.addSeparator();
        menu.addAction(tr("&Copy Path"), this, [this]() {
            auto sel = m_treeView->selectionModel()->selectedRows(0);
            auto paths = m_model->selectedEntryPaths(sel);
            if (!paths.isEmpty())
                QApplication::clipboard()->setText(paths.join('\n'));
        });

        // Properties
        menu.addAction(tr("Properties..."), this, [this]() {
            auto sel = m_treeView->selectionModel()->selectedRows(0);
            if (sel.isEmpty()) return;
            auto paths = m_model->selectedEntryPaths({sel[0]});
            if (paths.isEmpty()) return;
            const std::string key = paths[0].toStdString();
            const ArchiveEntry* found = nullptr;
            for (const auto& e : m_engine->ListContents())
                if (e.name == key || e.path == key) { found = &e; break; }
            if (!found) return;

            QDialog pd(this);
            pd.setWindowTitle(tr("Properties — %1").arg(paths[0]));
            pd.setMinimumWidth(420);
            auto* lay  = new QVBoxLayout(&pd);
            auto* form = new QFormLayout();
            form->setLabelAlignment(Qt::AlignRight);
            lay->addLayout(form);

            auto addRow = [&](const QString& label, const QString& value) {
                auto* lbl = new QLabel(value, &pd);
                lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
                form->addRow(label, lbl);
            };

            addRow(tr("Name:"), QString::fromUtf8(found->name.c_str()));
            addRow(tr("Path:"), QString::fromUtf8(found->path.c_str()));
            addRow(tr("Type:"), found->isDirectory ? tr("Folder") : tr("File"));
            if (!found->isDirectory) {
                addRow(tr("Size:"), tr("%1 bytes").arg(found->size));
                if (found->packedSize > 0)
                    addRow(tr("Packed:"), tr("%1 bytes").arg(found->packedSize));
                if (found->size > 0 && found->packedSize > 0) {
                    int pct = static_cast<int>((1.0 - static_cast<double>(found->packedSize) / found->size) * 100);
                    addRow(tr("Ratio:"), tr("%1% saved").arg(pct));
                }
                if (!found->compressionMethod.empty())
                    addRow(tr("Method:"), QString::fromUtf8(found->compressionMethod.c_str()));
                if (found->crc)
                    addRow(tr("CRC32:"), QString::asprintf("%08X", found->crc));
            }
            if (found->modified != std::chrono::system_clock::time_point{}) {
                time_t t = std::chrono::system_clock::to_time_t(found->modified);
                addRow(tr("Modified:"), QDateTime::fromSecsSinceEpoch(t).toString("yyyy-MM-dd HH:mm:ss"));
            }
            if (found->permissions) {
                uint32_t m = found->permissions;
                QString s;
                s += (m & 00400) ? 'r' : '-'; s += (m & 00200) ? 'w' : '-'; s += (m & 00100) ? 'x' : '-';
                s += (m & 00040) ? 'r' : '-'; s += (m & 00020) ? 'w' : '-'; s += (m & 00010) ? 'x' : '-';
                s += (m & 00004) ? 'r' : '-'; s += (m & 00002) ? 'w' : '-'; s += (m & 00001) ? 'x' : '-';
                addRow(tr("Permissions:"), s);
            }
            if (!found->comment.empty())
                addRow(tr("Comment:"), QString::fromUtf8(found->comment.c_str()));

            auto* btnRow = new QHBoxLayout();
            btnRow->addStretch();
            auto* closeBtn = new QPushButton(tr("Close"), &pd);
            connect(closeBtn, &QPushButton::clicked, &pd, &QDialog::accept);
            btnRow->addWidget(closeBtn);
            lay->addLayout(btnRow);
            pd.exec();
        });

        QAction* checksumsAct = menu.addAction(tr("Checksums..."), this, &MainWindow::onChecksums);
        checksumsAct->setEnabled(hasEngine && !selectionIsDir);
    }

    if (hasEngine && m_engine->SupportsCreation())
    {
        QAction* renameAct = menu.addAction(tr("Rename..."), this, [this]() {
            auto sel = m_treeView->selectionModel()->selectedRows(0);
            if (sel.isEmpty()) return;
            auto paths = m_model->selectedEntryPaths({sel[0]});
            if (paths.isEmpty()) return;

            QString oldName = paths[0];
            QString newName = QInputDialog::getText(this, tr("Rename"),
                tr("New name for %1:").arg(oldName),
                QLineEdit::Normal, oldName);
            if (newName.isEmpty() || newName == oldName) return;

            QString prefix = m_model->currentDir();
            if (!prefix.isEmpty()) prefix += "/";
            QString fullNew = prefix + newName;

            QDialog busyDlg(this, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
            busyDlg.setWindowTitle(tr("Renaming..."));
            busyDlg.setFixedSize(300, 80);
            auto* bLay = new QVBoxLayout(&busyDlg);
            auto* bLbl = new QLabel(tr("Renaming..."), &busyDlg);
            bLbl->setAlignment(Qt::AlignCenter);
            bLay->addWidget(bLbl);
            auto* bBar = new QProgressBar(&busyDlg);
            bBar->setRange(0, 0);
            bLay->addWidget(bBar);
            busyDlg.setWindowModality(Qt::ApplicationModal);
            busyDlg.show();
            QApplication::processEvents();

            std::atomic<bool> done{false};
            std::atomic<bool> ok{false};
            std::string oldStd = oldName.toStdString();
            std::string newStd = fullNew.toStdString();

            std::thread t([this, &done, &ok, &oldStd, &newStd]() {
                ok = m_engine->RenameEntry(oldStd, newStd);
                if (ok) ok = m_engine->Save();
                done = true;
            });

            while (!done)
                QApplication::processEvents(QEventLoop::AllEvents, 16);
            if (t.joinable()) t.join();

            if (ok)
                refreshFileList();
            else
                QMessageBox::warning(this, tr("Error"), tr("Rename failed."));
        });
        renameAct->setEnabled(hasSelection);

        QAction* commentAct = menu.addAction(tr("Comment..."), this, &MainWindow::onEntryComment);
        commentAct->setEnabled(hasSelection);

        QAction* delAct = menu.addAction(tr("Delete"), this, &MainWindow::onDelete);
        delAct->setEnabled(hasSelection);
    }

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void MainWindow::onBeginDrag()
{
    if (!m_engine) return;
    if (m_dragInProgress) return; // block re-entrant drag started by event loop inside drag->exec()

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths(sel);
    if (paths.isEmpty()) return;

    // Expand directories recursively
    auto allEntries = m_engine->ListContents();
    QStringList filePaths;

    for (const auto& p : paths)
    {
        std::string sp = p.toStdString();
        bool isDir = false;

        // Check for explicit directory entry (e.g. "folder/")
        for (const auto& e : allEntries)
            if (e.path == sp || e.path == sp + "/")
                { isDir = e.isDirectory; break; }

        // Check for implicit directory (no directory marker in the zip,
        // but files exist under this prefix)
        if (!isDir)
        {
            std::string prefix = sp + "/";
            for (const auto& e : allEntries)
                if (e.path.compare(0, prefix.size(), prefix) == 0)
                    { isDir = true; break; }
        }

        if (isDir)
        {
            std::string prefix = sp + "/";
            for (const auto& e : allEntries)
                if (!e.isDirectory && e.path.compare(0, prefix.size(), prefix) == 0)
                    filePaths << QString::fromUtf8(e.path.c_str());
        }
        else
        {
            filePaths << p;
        }
    }

    if (filePaths.isEmpty()) return;

    // Strip the current browsing prefix so dropped items appear as the user sees them
    QString prefix = m_model->currentDir();
    if (!prefix.isEmpty()) prefix += "/";

#ifdef _WIN32
    // Windows: VirtualFileDataObject
    VirtualFileDataObject* vfdo = new VirtualFileDataObject();
    for (const auto& fp : filePaths)
    {
        VirtualFileEntry ve;
        std::string fullPath = fp.toStdString();
        QString dragName = fp.startsWith(prefix) ? fp.mid(prefix.size()) : fp;
        ve.name = dragName.toStdWString();
        for (const auto& e : allEntries)
            if (e.path == fullPath) { ve.size = e.size; break; }
        ve.engine = m_engine.get();
        ve.archivePath = fullPath;
        vfdo->AddFile(ve);
    }
    if (vfdo->GetCount() > 0)
        StartVirtualDrag(vfdo, (HWND)winId());  // consumes the constructor ref via Release()
    else
        vfdo->Release();
#else
#ifdef Q_OS_MACOS
    // macOS: native file promises (lazy extraction on drop)
    // Return full relative path from fileNameForType: — macOS 15 may
    // create the subdirectory structure at the drop destination.
    {
        QByteArray buf;
        for (int i = 0; i < filePaths.size(); ++i) {
            std::string archivePath = filePaths[i].toStdString();
            QString dragName = filePaths[i];
            if (!prefix.isEmpty() && dragName.startsWith(prefix))
                dragName = dragName.mid(prefix.size());
            // Keep subdirectory structure for macOS 15 to recreate
            buf.append(archivePath.c_str(), archivePath.size() + 1);
            QByteArray dn = dragName.toUtf8();
            buf.append(dn.constData(), dn.size() + 1);
        }
        startMacFilePromiseDrag((void*)winId(), m_engine.get(),
                                buf.constData(), filePaths.size(), this);
    }
#else
    bool dragHandled = false;
#ifdef ZIPFX_HAVE_FUSE
    // Linux + FUSE: mount archive as a virtual filesystem, drag real paths lazily.
    // FUSE is used by default. Set ZIPFX_NO_FUSE=1 to force eager extraction.
    if (!qEnvironmentVariableIsSet("ZIPFX_NO_FUSE"))
    {
        // Abandon any previous FUSE mount that's still in its grace-period wait.
        // This allows a second drag to use FUSE immediately instead of falling
        // back to the slow eager extraction path.
        if (m_fuseThread.joinable())
        {
            m_fuseMount->abandon();
            m_fuseThread.join();
            m_fuseMount.reset();
        }

        // Lock BEFORE start() — the extraction inside start() now calls
        // processEvents(), which could re-enter onBeginDrag() and corrupt
        // the archive engine or spawn a second FUSE mount.
        m_dragInProgress = true;

        m_fuseMount = std::make_unique<FuseArchiveMount>(m_engine.get());
        FuseArchiveMount* mount = m_fuseMount.get();
        for (const auto& fp : filePaths)
        {
            QString dragName = fp.startsWith(prefix) ? fp.mid(prefix.size()) : fp;
            uint64_t sz = 0;
            uint32_t perm = 0644;
            for (const auto& e : allEntries)
                if (e.path == fp.toStdString()) { sz = e.size; perm = e.permissions ? e.permissions : 0644; break; }
            mount->addEntry(fp.toStdString(), dragName.toStdString(), sz, perm);
        }

        if (mount->start())
        {
            dragHandled = true;
            QList<QUrl> urls;
            for (const auto& fp : filePaths)
            {
                QString dragName = fp.startsWith(prefix) ? fp.mid(prefix.size()) : fp;
                urls << QUrl::fromLocalFile(
                    QString::fromStdString(mount->mountPoint()) + "/" + dragName);
            }
            QMimeData* mime = new QMimeData();
            mime->setUrls(urls);
            QDrag* drag = new QDrag(this);
            drag->setMimeData(mime);
            // Set an explicit pixmap so Wayland compositors show drag feedback.
            // A dark background ensures white text is visible on any surface.
            {
                int nFiles = filePaths.size();
                QString label = nFiles == 1
                    ? filePaths[0].section('/', -1)
                    : QStringLiteral("%1 files").arg(nFiles);
                QPixmap pm(256, 38);
                pm.fill(QColor(45, 45, 45));
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                int penWidth = pm.height() - 2;
                QPixmap iconPm(":/AppIcon.png");
                if (!iconPm.isNull())
                {
                    QPixmap scaled = iconPm.scaled(penWidth, penWidth, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    p.drawPixmap(2, (pm.height() - scaled.height()) / 2, scaled);
                }
                p.setPen(Qt::white);
                p.setFont(QFont("sans-serif", 11));
                int textLeft = iconPm.isNull() ? 4 : penWidth + 6;
                p.drawText(textLeft, 0, pm.width() - textLeft - 4, pm.height(),
                           Qt::AlignVCenter | Qt::AlignLeft, label);
                p.end();
                drag->setPixmap(pm);
                drag->setHotSpot({8, 16});
            }
            auto t0 = std::chrono::steady_clock::now();
            Qt::DropAction result = drag->exec(Qt::CopyAction);
            auto dt = std::chrono::steady_clock::now() - t0;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
            LOG_DBG("FuseArchiveMount: QDrag::exec returned %d after %lld ms",
                    static_cast<int>(result), (long long)ms);
            // Clear the re-entrancy guard NOW so the user can start a new
            // drag immediately (e.g. a second file from the same archive).
            // The abandon+join at the top of this block will tear down the
            // previous mount gracefully when the new drag arrives.
            m_dragInProgress = false;
            bool dropped = (result == Qt::CopyAction
                         || result == Qt::MoveAction
                         || result == Qt::LinkAction);
            m_fuseThread = std::thread([mount, dropped] {
                mount->unmount(!dropped);
            });
        }
        else
        {
            m_dragInProgress = false; // start() failed, unlock
            m_fuseMount.reset();
        }
    }
#endif // ZIPFX_HAVE_FUSE

    if (!dragHandled)
    {
        // Eager fallback: extract to temp dir before starting the drag.
        // Used when FUSE is unavailable at compile time or mount fails at runtime.
        //
        // Set m_dragInProgress NOW — before processEvents() is called inside the
        // extraction loop below — so that any re-entrant onBeginDrag() call (e.g.
        // from a mouse-move event processed by processEvents()) is blocked.
        // Without this, cascading re-entrant calls corrupt libzip / Qt heap state.
        m_dragInProgress = true;

        QTemporaryDir tmpRootObj(QStandardPaths::writableLocation(
            QStandardPaths::TempLocation) + "/ZipFX_Drag_XXXXXX");
        if (!tmpRootObj.isValid()) { m_dragInProgress = false; return; }
        tmpRootObj.setAutoRemove(false);
        QString tmpRoot = tmpRootObj.path() + "/";
        TempCleanup::registerPath(tmpRoot.toStdString());

        uint64_t totalBytesDrag = 0;
        for (const auto& fp : filePaths)
            for (const auto& e : allEntries)
                if (e.path == fp.toStdString())
                    { totalBytesDrag += e.packedSize > 0 ? e.packedSize : e.size; break; }

        ProgressInfo piDrag;
        piDrag.start(totalBytesDrag);

        QProgressDialog prog(tr("Preparing files for drag..."), tr("Cancel"),
            0, filePaths.size(), this);
        prog.setWindowModality(Qt::ApplicationModal);
        prog.show();

        QList<QUrl> urls;
        for (int i = 0; i < filePaths.size(); ++i)
        {
            if (prog.wasCanceled()) break;
            prog.setValue(i);

            std::string fp = filePaths[i].toStdString();

            piDrag.addBytes(0);
            for (const auto& e : allEntries)
                if (e.path == fp)
                    { piDrag.addBytes(e.packedSize > 0 ? e.packedSize : e.size); break; }
            {
                QString fname = filePaths[i];
                if (piDrag.shouldUpdate())
                {
                    piDrag.updateRate();
                    QString eta = piDrag.etaString();
                    fname += (eta.isEmpty() ? QString() : QChar('\n') + eta);
                }
                prog.setLabelText(fname);
            }
            QApplication::processEvents();

            QString fpQ = filePaths[i];
            QString dragName = fpQ.startsWith(prefix) ? fpQ.mid(prefix.size()) : fpQ;
            QString destPath = tmpRoot + dragName;
            QDir().mkpath(QFileInfo(destPath).path());

            if (m_engine->Extract(fp, destPath.toStdString()))
                urls << QUrl::fromLocalFile(destPath);
        }
        prog.close();

        if (!urls.isEmpty())
        {
            QMimeData* mime = new QMimeData();
            mime->setUrls(urls);
            QDrag* drag = new QDrag(this);
            drag->setMimeData(mime);
            // Same drag pixmap as the FUSE path above
            {
                int nFiles = filePaths.size();
                QString label = nFiles == 1
                    ? filePaths[0].section('/', -1)
                    : QStringLiteral("%1 files").arg(nFiles);
                QPixmap pm(256, 38);
                pm.fill(QColor(45, 45, 45));
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                int penWidth = pm.height() - 2;
                QPixmap iconPm(":/AppIcon.png");
                if (!iconPm.isNull())
                {
                    QPixmap scaled = iconPm.scaled(penWidth, penWidth, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    p.drawPixmap(2, (pm.height() - scaled.height()) / 2, scaled);
                }
                p.setPen(Qt::white);
                p.setFont(QFont("sans-serif", 11));
                int textLeft = iconPm.isNull() ? 4 : penWidth + 6;
                p.drawText(textLeft, 0, pm.width() - textLeft - 4, pm.height(),
                           Qt::AlignVCenter | Qt::AlignLeft, label);
                p.end();
                drag->setPixmap(pm);
                drag->setHotSpot({8, 16});
            }
            drag->exec(Qt::CopyAction);
        }

        m_dragInProgress = false; // always clear, whether drag happened or was cancelled
    }
#endif // Q_OS_MACOS
#endif // _WIN32
}

// ── macOS: handle "Open with" / double-click in Finder ─────────────
// QFileOpenEvent is dispatched to qApp, not to individual windows.
// We install this window as an event filter on qApp (in the constructor)
// so Finder open events reach us here.
bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == qApp && event->type() == QEvent::FileOpen)
    {
        auto* foe = static_cast<QFileOpenEvent*>(event);
        if (foe && !foe->file().isEmpty())
        {
            if (m_currentPath.empty())
                openArchive(foe->file());
            else
            {
                auto* w = new MainWindow(foe->file());
                w->show();
            }
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

// ── Drag & Drop ────────────────────────────────────────────────────────
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    // Reject self-drags: a drag that originated from this window carrying FUSE
    // URLs would trigger dropEvent → doAddPaths → Save() while drag->exec() is
    // still on the call stack, corrupting the zip handle.
    if (event->source() != nullptr) { event->ignore(); return; }
    // Reject drops when an archive is open but doesn't support adding files
    if (m_engine && !m_engine->SupportsCreation())
    {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto* mime = event->mimeData();
    if (!mime->hasUrls()) return;

    QList<QUrl> urls = mime->urls();
    if (urls.isEmpty()) return;

    QStringList paths;
    for (const auto& url : urls)
        if (url.isLocalFile())
            paths << url.toLocalFile();

    if (paths.isEmpty()) return;

    if (!m_engine)
    {
        // No archive open — try to open the first file
        openArchive(paths[0]);
        return;
    }

    if (!m_engine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("Read-only format."));
        return;
    }

    QTimer::singleShot(0, this, [this, paths]() { doAddPaths(paths); });
}

// ── Helpers ────────────────────────────────────────────────────────────
void MainWindow::refreshFileList()
{
    if (!m_engine)
    {
        m_model->clear();
        return;
    }

    auto entries = m_engine->ListContents();
    m_model->setEntries(entries);

    bool hasPacked = false, hasCRC = false, hasPerms = false, hasComment = false;
    for (const auto& e : entries)
    {
        if (e.isDirectory) continue;
        if (e.packedSize > 0 && e.packedSize != e.size) hasPacked = true;
        if (e.crc != 0) hasCRC = true;
        if (e.permissions != 0) hasPerms = true;
        if (!e.comment.empty()) hasComment = true;
    }
    auto autoHide = [&](int col, bool hasData) {
        if (m_userManagedCols.count(col) == 0)
            m_treeView->setColumnHidden(col, !hasData);
    };
    autoHide(FileListModel::ColPacked,      hasPacked);
    autoHide(FileListModel::ColCRC,         hasCRC);
    autoHide(FileListModel::ColPermissions, hasPerms);
    autoHide(FileListModel::ColComment,     hasComment);
    {
        bool hasMethod = false, hasRatio = false;
        for (const auto& e : entries)
        {
            if (e.isDirectory) continue;
            if (!e.compressionMethod.empty()) hasMethod = true;
            if (e.packedSize > 0 && e.packedSize != e.size) hasRatio = true;
        }
        autoHide(FileListModel::ColRatio,  hasRatio);
        autoHide(FileListModel::ColMethod, hasMethod);
    }

    // Restore saved column widths (or apply defaults on first run)
    {
        static const int kDefaultWidths[FileListModel::ColCount] = {
            250,  // Name
            80,   // Size
            80,   // Packed
            55,   // Ratio
            80,   // Method
            60,   // Type
            130,  // Modified
            90,   // CRC
            80,   // Permissions
            120,  // Comment
        };
        QSettings s;
        for (int col = 0; col < FileListModel::ColCount; ++col)
        {
            if (m_treeView->isColumnHidden(col)) continue;
            QString name = m_model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
            QString key  = "columns/" + name + "/width";
            int w = s.contains(key) ? s.value(key).toInt() : kDefaultWidths[col];
            m_treeView->setColumnWidth(col, w);
        }
    }

    m_archiveCommentAct->setEnabled(m_engine && m_engine->supportsArchiveComment());
    const bool canEncrypt = m_engine && m_engine->SupportsEncryption();
    m_passwordAct->setEnabled(true);
    m_setPasswordAct->setEnabled(canEncrypt);

    updateStatusBar();
    setWindowTitle(tr("ZipFX — %1").arg(
        QString::fromStdString(m_currentPath)));
}

// ── Password manager helpers ───────────────────────────────────────────
void MainWindow::savePassword(const QString& archive, const QString& password)
{
    // Keep an index of which archives have passwords so the dialog can list them.
    // The actual secrets live in the OS keychain (KeychainHelper).
    QSettings s;
    QStringList known = s.value("passwordManager/archives").toStringList();
    if (password.isEmpty())
    {
        KeychainHelper::remove(archive);
        known.removeAll(archive);
    }
    else
    {
        if (KeychainHelper::save(archive, password))
        {
            if (!known.contains(archive)) known.append(archive);
        }
        else
        {
            QMessageBox::warning(this, tr("Password Not Saved"),
                tr("No secure credential store is available on this system "
                   "(libsecret not found), so the password for \"%1\" was not "
                   "saved. You will need to re-enter it next time.")
                    .arg(QFileInfo(archive).fileName()));
        }
    }
    s.setValue("passwordManager/archives", known);
}

QString MainWindow::loadPassword(const QString& archive)
{
    return KeychainHelper::load(archive);
}

// ── Feature 14: preview pane ───────────────────────────────────────────
void MainWindow::updatePreview()
{
    if (!m_previewPanel || !m_previewPanel->isVisible() || !m_engine)
        return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.size() != 1)
    {
        m_previewPanel->showMessage(tr("No selection"));
        return;
    }

    auto paths = m_model->selectedEntryPaths({sel[0]});
    if (paths.isEmpty()) return;

    const QString entryPath = paths[0];
    const std::string entryStd = entryPath.toStdString();

    uint64_t entrySize = 0;
    bool isDir = false;
    for (const auto& e : m_engine->ListContents())
    {
        if (e.name == entryStd || e.path == entryStd)
        {
            entrySize = e.size;
            isDir = e.isDirectory;
            break;
        }
    }

    m_previewPanel->showEntry(*m_engine, entryPath, entrySize, isDir);
}

// ── Feature 11: Find Files dialog ─────────────────────────────────────
void MainWindow::onFindFiles()
{
    if (!m_engine || !m_engine->IsOpen())
    {
        QMessageBox::information(this, tr("Find"), tr("No archive open."));
        return;
    }

    auto entries = m_engine->ListContents(); // copy so dialog outlives engine changes
    FindFilesDialog dlg(entries, this);

    connect(&dlg, &FindFilesDialog::entryActivated, this, [this](const QString& path) {
        // Switch to flat mode and filter by filename so the entry is visible
        m_model->setFlatMode(true);
        if (m_flatAct) m_flatAct->setChecked(true);
        m_upBtn->setVisible(false);
        QString fname = QFileInfo(path).fileName();
        m_searchBox->setVisible(true);
        m_searchBox->setText(fname);
        m_model->setFilterString(fname);
    });
    connect(&dlg, &FindFilesDialog::entryExtractRequested, this, [this](const QString& path) {
        if (!m_engine) return;
        QString startDir = m_currentPath.empty()
            ? QString()
            : QFileInfo(QString::fromStdString(m_currentPath)).absolutePath();
        QString dest = QFileDialog::getExistingDirectory(this, tr("Extract to"), startDir);
        if (dest.isEmpty()) return;
        QDir().mkpath(dest + "/" + QFileInfo(path).path());
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool ok = m_engine->Extract(path.toStdString(),
                                    (dest + "/" + path).toStdString());
        QApplication::restoreOverrideCursor();
        if (ok)
            statusBar()->showMessage(tr("Extracted: %1").arg(path), 3000);
        else
            QMessageBox::warning(this, tr("Error"),
                tr("Failed to extract \"%1\".").arg(path));
    });

    dlg.exec();
}

// ── Feature 12: Archive conversion ────────────────────────────────────
void MainWindow::onConvertArchive()
{
    if (!m_engine || !m_engine->IsOpen())
    {
        QMessageBox::information(this, tr("Convert"), tr("No archive open."));
        return;
    }

    // Format picker dialog
    QDialog pickDlg(this);
    pickDlg.setWindowTitle(tr("Convert Archive"));
    auto* lay = new QVBoxLayout(&pickDlg);
    auto* form = new QFormLayout();

    auto* fmtCombo = new QComboBox(&pickDlg);
    const QStringList fmtNames = {"ZIP", "7-Zip (7z)", "TAR.GZ", "TAR.BZ2", "TAR.XZ"};
    const QStringList fmtKeys  = {"zip", "7z", "tar.gz", "tar.bz2", "tar.xz"};
    for (int i = 0; i < fmtNames.size(); ++i) fmtCombo->addItem(fmtNames[i], fmtKeys[i]);
    form->addRow(tr("Target format:"), fmtCombo);

    QFileInfo srcFi(QString::fromStdString(m_currentPath));
    QString srcBase = srcFi.completeBaseName();
    while (srcBase.contains('.')) srcBase = QFileInfo(srcBase).completeBaseName();

    auto* pathEdit = new QLineEdit(srcFi.dir().filePath(srcBase + "_converted.zip"), &pickDlg);
    auto* browseBtn = new QPushButton(tr("Browse..."), &pickDlg);
    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseBtn);
    form->addRow(tr("Output path:"), pathRow);
    lay->addLayout(form);

    connect(fmtCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &pickDlg,
        [&](int idx) {
            QString ext = fmtKeys[idx];
            pathEdit->setText(srcFi.dir().filePath(srcBase + "." + ext));
        });
    connect(browseBtn, &QPushButton::clicked, &pickDlg, [&]() {
        QString p = QFileDialog::getSaveFileName(&pickDlg, tr("Save Converted Archive"),
                                                 pathEdit->text());
        if (!p.isEmpty()) pathEdit->setText(p);
    });

    auto* btnRow = new QHBoxLayout();
    auto* okBtn     = new QPushButton(tr("Convert"), &pickDlg);
    auto* cancelBtn = new QPushButton(tr("Cancel"),  &pickDlg);
    okBtn->setDefault(true);
    connect(okBtn,     &QPushButton::clicked, &pickDlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &pickDlg, &QDialog::reject);
    btnRow->addStretch(); btnRow->addWidget(okBtn); btnRow->addWidget(cancelBtn);
    lay->addLayout(btnRow);

    if (pickDlg.exec() != QDialog::Accepted) return;

    const QString outPath = pathEdit->text().trimmed();
    const QString fmt     = fmtCombo->currentData().toString();
    if (outPath.isEmpty()) return;

    auto targetEngine = ArchiveEngineFactory::CreateForFormat(fmt.toStdString());
    if (!targetEngine || !targetEngine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("Target format not supported for writing."));
        return;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not create temporary directory."));
        return;
    }

    auto entries = m_engine->ListContents();
    QProgressDialog prog(tr("Extracting..."), tr("Cancel"), 0,
                         static_cast<int>(entries.size()), this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.show();

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        if (prog.wasCanceled()) return;
        prog.setValue(i);
        const auto& e = entries[i];
        if (e.isDirectory) continue;
        if (!ArchiveEngine::isSafeEntryName(e.path)) continue;
        prog.setLabelText(QString::fromUtf8(e.name.c_str()));
        QApplication::processEvents();
        QString dest = tempDir.path() + "/" + QString::fromUtf8(e.path.c_str());
        QDir().mkpath(QFileInfo(dest).path());
        m_engine->Extract(e.path, dest.toStdString());
    }

    prog.setLabelText(tr("Creating archive..."));
    prog.setRange(0, 0);
    QApplication::processEvents();

    if (!targetEngine->Create(outPath.toStdString()))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not create target archive."));
        return;
    }

    QDir tempQDir(tempDir.path());
    QDirIterator it(tempDir.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        if (prog.wasCanceled()) return;
        prog.setLabelText(it.fileName());
        QApplication::processEvents();
        QString rel = tempQDir.relativeFilePath(it.filePath());
        targetEngine->AddFile(it.filePath().toStdString(), rel.toStdString());
    }

    prog.setLabelText(tr("Saving..."));
    QApplication::processEvents();

    if (!targetEngine->Save())
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to save converted archive."));
        return;
    }

    prog.close();
    QMessageBox::information(this, tr("Convert"),
        tr("Converted successfully:\n%1").arg(outPath));
}

// ── Feature 15: Archive repair ────────────────────────────────────────
void MainWindow::onRepairArchive()
{
    if (!m_engine || !m_engine->IsOpen())
    {
        QMessageBox::information(this, tr("Repair"), tr("No archive open."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Repair Archive"));
    dlg.setMinimumSize(520, 340);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(
        tr("Attempts to recover readable files from a corrupted archive."), &dlg));

    auto* log = new QTextEdit(&dlg);
    log->setReadOnly(true);
    log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(log, 1);

    auto* btnRow = new QHBoxLayout();
    auto* repairBtn = new QPushButton(tr("Start Repair"), &dlg);
    repairBtn->setDefault(true);
    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addStretch(); btnRow->addWidget(repairBtn); btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    connect(repairBtn, &QPushButton::clicked, &dlg, [&]() {
        repairBtn->setEnabled(false);
        log->clear();

        log->append(tr("Testing archive integrity..."));
        QApplication::processEvents();

        bool ok = m_engine->TestIntegrity(nullptr, nullptr);
        if (ok)
        {
            log->append(tr("Integrity test PASSED — archive appears undamaged."));
            repairBtn->setEnabled(true);
            return;
        }

        log->append(tr("Errors detected. Extracting recoverable files..."));
        QApplication::processEvents();

        QTemporaryDir tempDir;
        if (!tempDir.isValid())
        {
            log->append(tr("ERROR: Cannot create temporary directory."));
            repairBtn->setEnabled(true);
            return;
        }

        auto entries = m_engine->ListContents();
        int recovered = 0, failed = 0;
        for (const auto& e : entries)
        {
            if (e.isDirectory) continue;
            if (!ArchiveEngine::isSafeEntryName(e.path)) { failed++; continue; }
            QString dest = tempDir.path() + "/" + QString::fromUtf8(e.path.c_str());
            QDir().mkpath(QFileInfo(dest).path());
            if (m_engine->Extract(e.path, dest.toStdString()))
                recovered++;
            else
            {
                log->append(tr("  FAILED: %1").arg(QString::fromUtf8(e.path.c_str())));
                failed++;
            }
            QApplication::processEvents();
        }

        log->append(tr("Recovered %1 file(s), failed: %2.").arg(recovered).arg(failed));

        if (recovered == 0)
        {
            log->append(tr("No files could be recovered."));
            repairBtn->setEnabled(true);
            return;
        }

        QString savePath = QFileDialog::getSaveFileName(
            &dlg, tr("Save Recovered Files"),
            QFileInfo(QString::fromStdString(m_currentPath)).dir()
                .filePath(QFileInfo(QString::fromStdString(m_currentPath)).completeBaseName()
                          + "_recovered.zip"),
            tr("ZIP (*.zip);;7-Zip (*.7z);;TAR.GZ (*.tar.gz);;All files (*.*)"));

        if (!savePath.isEmpty())
        {
            QString ext = QFileInfo(savePath).suffix().toLower();
            std::string fmt = "zip";
            if (ext == "7z")   fmt = "7z";
            else if (savePath.toLower().endsWith(".tar.gz"))  fmt = "tar.gz";
            else if (savePath.toLower().endsWith(".tar.bz2")) fmt = "tar.bz2";
            else if (savePath.toLower().endsWith(".tar.xz"))  fmt = "tar.xz";
            auto eng = ArchiveEngineFactory::CreateForFormat(fmt);
            if (eng && eng->SupportsCreation() && eng->Create(savePath.toStdString()))
            {
                QDir tmp(tempDir.path());
                QDirIterator it(tempDir.path(), QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext())
                {
                    it.next();
                    eng->AddFile(it.filePath().toStdString(),
                                 tmp.relativeFilePath(it.filePath()).toStdString());
                }
                if (eng->Save())
                    log->append(tr("Saved: %1").arg(savePath));
                else
                    log->append(tr("ERROR: Failed to write recovered archive."));
            }
        }

        repairBtn->setEnabled(true);
    });

    dlg.exec();
}

// ── Feature 16: Batch operations ──────────────────────────────────────
void MainWindow::onBatchOps()
{
    BatchOpsDialog dlg(this);
    dlg.exec();
}

// ── Feature 17: Password manager ──────────────────────────────────────
void MainWindow::onPasswordManager()
{
    PasswordManagerDialog dlg(this);
    dlg.exec();
}

// ── Shell-add (from shell extension) ──────────────────────────────────────
void MainWindow::shellAdd(const QStringList& files)
{
    if (!m_engine || !m_engine->IsOpen())
    {
        // No archive open — show the Create Archive dialog first
        auto* dlg = new CreateArchiveDialog(this);
        if (dlg->exec() != QDialog::Accepted)
        {
            dlg->deleteLater();
            return;
        }
        CreateArchiveResult res = dlg->result();
        dlg->deleteLater();

        auto eng = ArchiveEngineFactory::CreateForFormat(res.format.toStdString());
        if (!eng || !eng->Create(res.path.toStdString()))
        {
            QMessageBox::warning(this, tr("Error"), tr("Could not create archive."));
            return;
        }
        m_engine      = std::move(eng);
        m_currentPath = res.path.toStdString();
        addRecentFile(res.path);
        setWindowTitle("ZipFX — " + res.path);
    }

    doAddPaths(files);
}

// ── Feature 21: Recovery record ────────────────────────────────────────────
void MainWindow::onAddRecoveryRecord()
{
    if (m_currentPath.empty())
    {
        QMessageBox::information(this, tr("Recovery Record"), tr("No archive open."));
        return;
    }

    const QString archivePath = QString::fromStdString(m_currentPath);

    // For RAR archives, also offer rar.exe path
    const QString ext = QFileInfo(archivePath).suffix().toLower();
    const bool isRar  = (ext == "rar");

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Recovery Record"));
    auto* lay  = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(
        tr("Creates a .rec sidecar file containing XOR parity blocks.\n"
           "The sidecar allows corrupted bytes to be reconstructed\n"
           "if individual data blocks are damaged."), &dlg));

    auto* form = new QFormLayout();

    auto* recPctSpin = new QSpinBox(&dlg);
    recPctSpin->setRange(1, 30);
    recPctSpin->setValue(5);
    recPctSpin->setSuffix(tr("% of data blocks"));
    form->addRow(tr("Recovery size:"), recPctSpin);

    auto* blockSzCombo = new QComboBox(&dlg);
    blockSzCombo->addItem(tr("32 KB"),  32768);
    blockSzCombo->addItem(tr("64 KB"),  65536);
    blockSzCombo->addItem(tr("128 KB"), 131072);
    blockSzCombo->setCurrentIndex(1);
    form->addRow(tr("Block size:"), blockSzCombo);

    lay->addLayout(form);

    if (isRar)
    {
        lay->addWidget(new QLabel(
            tr("RAR tip: rar.exe also supports native recovery records (rr N%)."), &dlg));

        auto* rarBtn = new QPushButton(tr("Add via rar.exe (if installed)..."), &dlg);
        lay->addWidget(rarBtn);
        connect(rarBtn, &QPushButton::clicked, &dlg, [&]() {
            QString rarExe;
#ifdef _WIN32
            // Look in standard WinRAR locations
            for (const auto* p : {R"(C:\Program Files\WinRAR\Rar.exe)",
                                   R"(C:\Program Files (x86)\WinRAR\Rar.exe)"})
                if (QFileInfo::exists(QString::fromLatin1(p)))
                    { rarExe = QString::fromLatin1(p); break; }
#else
            rarExe = "rar";
#endif
            if (rarExe.isEmpty())
            {
                QMessageBox::warning(&dlg, tr("rar.exe"),
                    tr("rar.exe not found. Install WinRAR or use the sidecar method."));
                return;
            }
            QProcess proc;
            proc.start(rarExe, {"rr" + QString::number(recPctSpin->value()) + "%",
                                 archivePath});
            if (!proc.waitForFinished(60000))
            {
                QMessageBox::warning(&dlg, tr("rar.exe"), tr("rar.exe timed out."));
                return;
            }
            QMessageBox::information(&dlg, tr("rar.exe"),
                proc.exitCode() == 0
                    ? tr("Native RAR recovery record added.")
                    : tr("rar.exe returned error %1:\n%2")
                        .arg(proc.exitCode())
                        .arg(QString::fromLocal8Bit(proc.readAllStandardError())));
        });
    }

    auto* log = new QTextEdit(&dlg);
    log->setReadOnly(true);
    log->setMaximumHeight(80);
    lay->addWidget(log);

    auto* btnRow = new QHBoxLayout();
    auto* goBtn    = new QPushButton(tr("Create Sidecar (.rec)"), &dlg);
    auto* closeBtn = new QPushButton(tr("Close"), &dlg);
    goBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addStretch(); btnRow->addWidget(goBtn); btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    connect(goBtn, &QPushButton::clicked, &dlg, [&]() {
        goBtn->setEnabled(false);
        log->clear();

        RecoveryRecord::Options opts;
        opts.recoveryPercent = recPctSpin->value();
        opts.blockSize       = static_cast<uint32_t>(
            blockSzCombo->currentData().toInt());

        QProgressDialog prog(tr("Creating recovery record..."), {}, 0, 100, this);
        prog.setWindowModality(Qt::ApplicationModal);
        prog.show();

        QString err = RecoveryRecord::create(archivePath, opts, [&](int pct) {
            prog.setValue(pct);
            QApplication::processEvents();
        });

        prog.close();
        if (err.isEmpty())
        {
            log->append(tr("Sidecar created: %1")
                .arg(RecoveryRecord::sidecarPath(archivePath)));
            log->append(tr("Recovery: %1% of data blocks (%2).")
                .arg(opts.recoveryPercent)
                .arg(blockSzCombo->currentText()));
        }
        else
        {
            log->append(tr("ERROR: %1").arg(err));
        }
        goBtn->setEnabled(true);
    });

    dlg.exec();
}

void MainWindow::onVerifyRecoveryRecord()
{
    if (m_currentPath.empty())
    {
        QMessageBox::information(this, tr("Recovery Record"), tr("No archive open."));
        return;
    }

    const QString archivePath = QString::fromStdString(m_currentPath);

    // For RAR, also offer rar.exe repair
    const QString ext = QFileInfo(archivePath).suffix().toLower();
    const bool isRar  = (ext == "rar");

    if (!RecoveryRecord::hasSidecar(archivePath))
    {
        QString msg = tr("No sidecar (.rec) file found for this archive.");
        if (isRar)
        {
            // Try rar.exe repair as fallback
            auto ans = QMessageBox::question(this, tr("Recovery Record"),
                msg + "\n\n" + tr("Try repairing with rar.exe (native RAR recovery record)?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ans == QMessageBox::Yes)
            {
                QString rarExe;
#ifdef _WIN32
                for (const auto* p : {R"(C:\Program Files\WinRAR\Rar.exe)",
                                       R"(C:\Program Files (x86)\WinRAR\Rar.exe)"})
                    if (QFileInfo::exists(QString::fromLatin1(p)))
                        { rarExe = QString::fromLatin1(p); break; }
#else
                rarExe = "rar";
#endif
                if (rarExe.isEmpty())
                {
                    QMessageBox::warning(this, tr("rar.exe"), tr("rar.exe not found."));
                    return;
                }
                QProcess proc;
                proc.start(rarExe, {"r", archivePath});
                proc.waitForFinished(120000);
                QMessageBox::information(this, tr("rar.exe"),
                    proc.exitCode() == 0
                        ? tr("RAR repair completed.")
                        : tr("rar.exe returned error %1:\n%2")
                            .arg(proc.exitCode())
                            .arg(QString::fromLocal8Bit(proc.readAllStandardError())));
            }
            return;
        }
        QMessageBox::information(this, tr("Recovery Record"), msg);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Verify/Repair with Recovery Record"));
    dlg.setMinimumSize(500, 320);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(
        tr("Checks the archive against its .rec sidecar file.\n"
           "If corruption is detected and is within recovery capacity,\n"
           "the archive will be repaired in-place."), &dlg));

    auto* log = new QTextEdit(&dlg);
    log->setReadOnly(true);
    log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(log, 1);

    auto* btnRow    = new QHBoxLayout();
    auto* verifyBtn = new QPushButton(tr("Verify Only"), &dlg);
    auto* repairBtn = new QPushButton(tr("Verify && Repair"), &dlg);
    auto* closeBtn  = new QPushButton(tr("Close"), &dlg);
    verifyBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(verifyBtn); btnRow->addWidget(repairBtn);
    btnRow->addStretch(); btnRow->addWidget(closeBtn);
    lay->addLayout(btnRow);

    auto doVerify = [&](bool repair) {
        verifyBtn->setEnabled(false);
        repairBtn->setEnabled(false);
        log->clear();

        QProgressDialog prog(tr("Verifying..."), {}, 0, 100, this);
        prog.setWindowModality(Qt::ApplicationModal);
        prog.show();

        auto result = RecoveryRecord::verify(archivePath, repair, [&](int pct) {
            prog.setValue(pct);
            QApplication::processEvents();
        });

        prog.close();

        log->append(tr("Total blocks : %1").arg(result.totalBlocks));
        log->append(tr("Bad blocks   : %1").arg(result.badBlocks));

        if (result.badBlocks == 0)
        {
            log->append(tr("\nArchive integrity: OK — no corruption detected."));
        }
        else if (repair)
        {
            log->append(tr("Repaired     : %1").arg(result.repairedBlocks));
            log->append(tr("Unrepairable : %1").arg(result.badBlocks - result.repairedBlocks));
            if (result.ok)
                log->append(tr("\nAll corrupted blocks repaired successfully."));
            else
                log->append(tr("\nERROR: %1").arg(result.errorMessage));
        }
        else
        {
            log->append(tr("\nCorruption detected. Use 'Verify && Repair' to attempt repair."));
        }

        verifyBtn->setEnabled(true);
        repairBtn->setEnabled(true);
    };

    connect(verifyBtn, &QPushButton::clicked, &dlg, [&]() { doVerify(false); });
    connect(repairBtn, &QPushButton::clicked, &dlg, [&]() { doVerify(true); });

    dlg.exec();
}

// ── Shell extension install / uninstall ────────────────────────────────────
void MainWindow::installShellExtension(bool install)
{
#ifndef _WIN32
    Q_UNUSED(install)
    QMessageBox::information(this, tr("Shell Extension"),
        tr("Shell extensions are only available on Windows."));
    return;
#else
    // Look for the DLL next to the executable
    const QString dllPath = QApplication::applicationDirPath() + "/ZipFXShellExt.dll";
    if (!QFileInfo::exists(dllPath))
    {
        QMessageBox::warning(this, tr("Shell Extension"),
            tr("ZipFXShellExt.dll not found in the application directory.\n"
               "Please build the shell extension target first."));
        return;
    }

    const QString verb = install ? "install" : "uninstall";

    // Use regsvr32 which calls DllRegisterServer / DllUnregisterServer
    QStringList args;
    if (!install) args << "/u";
    args << "/s" << QDir::toNativeSeparators(dllPath);

    QProcess proc;
    proc.start("regsvr32", args);
    bool ok = proc.waitForFinished(10000);

    if (!ok || proc.exitCode() != 0)
    {
        QMessageBox::warning(this, tr("Shell Extension"),
            tr("regsvr32 failed (exit %1).\n"
               "You may need to run ZipFX as administrator for this operation.")
                .arg(proc.exitCode()));
        return;
    }

    QMessageBox::information(this, tr("Shell Extension"),
        install
            ? tr("Shell extension installed. Right-click any file in Explorer to use it.\n"
                 "You may need to restart Explorer (or sign out) for the menu to appear.")
            : tr("Shell extension uninstalled."));
#endif
}

// ── Linux: register desktop file & icon on every launch ────────────
#if defined(__linux__)
void MainWindow::registerFileAssociationsLinux()
{
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    const QString appsDir = dataDir + "/applications";
    const QString hicolorDir = dataDir + "/icons/hicolor";
    const QString icon256Dir = hicolorDir + "/256x256/apps";
    const QString icon64Dir = hicolorDir + "/64x64/apps";
    const QString desktopPath = appsDir + "/zipfx.desktop";
    const QString desktopPathUpper = appsDir + "/ZipFX.desktop";
    const QString iconName = "zipfx.png";

    // Current executable path (AppImage or regular binary)
    const QString execPath = [&]() -> QString {
        const QString appImg = qEnvironmentVariable("APPIMAGE");
        if (!appImg.isEmpty())
            return appImg;
        return QApplication::applicationFilePath();
    }();

    // ── Icon (install to multiple sizes for best matching) ──
    const QString srcIcon = QApplication::applicationDirPath() + "/AppIcon.png";
    if (QFile::exists(srcIcon))
    {
        for (const auto& dir : {icon256Dir, icon64Dir})
        {
            QDir().mkpath(dir);
            const QString dst = dir + "/" + iconName;
            QFile::remove(dst);
            QFile::copy(srcIcon, dst);
        }
    }

    // Never create a user-local index.theme — the system hicolor theme
    // already has the full Directories= list and is authoritative. A
    // minimal user-local version with Directories= missing or Hidden=true
    // would silently disable hicolor and break all icon lookups.
    // gtk-update-icon-cache below will read the system index.theme and
    // index our icons correctly.

    // ── Desktop file (always rewrite to pick up field additions) ──
    QDir().mkpath(appsDir);
    {
        QFile df(desktopPath);
        if (df.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QByteArray contents;
            contents += "[Desktop Entry]\n";
            contents += "Type=Application\n";
            contents += "Name=ZipFX\n";
            contents += "Comment=Multi-format archive manager\n";
            contents += "Exec=\"" + execPath.toUtf8() + "\" %f\n";
            contents += "Icon=zipfx\n";
            contents += "StartupWMClass=zipfx\n";
            contents += "Terminal=false\n";
            contents += "Categories=Archiving;Utility;\n";
            df.write(contents);
            df.close();
        }
    }

    // Symlink ZipFX.desktop → zipfx.desktop so the portal finds it regardless
    // of app_id casing.
    if (!QFile::exists(desktopPathUpper))
        QFile::link("zipfx.desktop", desktopPathUpper);

    // Update icon cache and desktop database (non-fatal if unavailable).
    // Do NOT create a user-local index.theme — the system theme is authoritative.
    QProcess::startDetached("gtk-update-icon-cache",
        {"--ignore-theme-index", "--quiet", hicolorDir});
    QProcess::startDetached("update-desktop-database",
        {appsDir, "--quiet"});
}

// ── Windows: register file associations on first launch ────────────
#elif defined(_WIN32)
#include <QSettings>
#include <QDir>
#include <shlwapi.h>
#include <windows.h>

void MainWindow::registerFileAssociations()
{
    QSettings s;
    if (s.value("assoc/registered", false).toBool())
        return;

    QString appPath = QDir::toNativeSeparators(QApplication::applicationFilePath());
    static const char* kExts[] = { ZIPFX_ARCHIVE_EXTS(ZIPFX_EXT_N) nullptr };

    for (const char* const* p = kExts; *p; ++p)
    {
        const QString ext = QString::fromLatin1(*p);
        HKEY hKey;
        QString key = "ZipFX\\" + ext;
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, ("." + ext).toStdString().c_str(),
                0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            std::string val = key.toStdString();
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(val.c_str()), val.size() + 1);
            RegCloseKey(hKey);
        }

        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, key.toStdString().c_str(),
                0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>("ZipFX Archive"), 15);
            RegCloseKey(hKey);
        }

        QString cmdKey = key + "\\shell\\open\\command";
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, cmdKey.toStdString().c_str(),
                0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            std::string cmd = ("\"" + appPath + "\" \"%1\"").toStdString();
            RegSetValueExA(hKey, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(cmd.c_str()), cmd.size() + 1);
            RegCloseKey(hKey);
        }
    }

    s.setValue("assoc/registered", true);
    // Notify Explorer of association changes via dynamic load (avoids MinGW header issues)
#define SHCNE_ASSOCCHANGED 0x8000000L
#define SHCNF_IDLIST 0
    typedef void (WINAPI *SHChangeNotify_t)(LONG, UINT, LPCVOID, LPCVOID);
    auto shell32 = LoadLibraryA("shell32.dll");
    if (shell32)
    {
        auto fn = reinterpret_cast<SHChangeNotify_t>(
            GetProcAddress(shell32, "SHChangeNotify"));
        if (fn)
            fn(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        FreeLibrary(shell32);
    }
}
#elif defined(__APPLE__)
void MainWindow::registerFileAssociationsMac()
{
    QSettings s;
    if (s.value("assoc/registered_mac", false).toBool())
        return;

    // Launch Services only auto-scans /Applications and ~/Applications.
    // Force it to read our bundle's CFBundleDocumentTypes/UTExportedTypeDeclarations
    // now, regardless of where the .app lives (dev build, Downloads, etc.).
    QDir d(QApplication::applicationDirPath()); // .app/Contents/MacOS
    d.cdUp(); d.cdUp();                          // → .app
    const QString lsregister =
        "/System/Library/Frameworks/CoreServices.framework"
        "/Versions/A/Frameworks/LaunchServices.framework"
        "/Versions/A/Support/lsregister";
    QProcess::startDetached(lsregister, {"-f", d.absolutePath()});

    s.setValue("assoc/registered_mac", true);
}
#endif

void MainWindow::onChecksums()
{
    if (!m_engine) return;

    auto sel = m_treeView->selectionModel()->selectedRows(0);
    if (sel.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths(sel);
    const auto& entries = m_engine->ListContents();

    std::vector<std::string> names;
    std::vector<const ArchiveEntry*> entryPtrs;
    for (const auto& p : paths)
    {
        std::string key = p.toStdString();
        for (const auto& e : entries)
        {
            if (e.name == key || e.path == key)
            {
                if (!e.isDirectory)
                {
                    names.push_back(key);
                    entryPtrs.push_back(&e);
                }
                break;
            }
        }
    }

    if (names.empty()) return;

    ChecksumsDialog dlg(m_engine.get(), names, entryPtrs, this);
    dlg.exec();
}
