#include "MainWindow.h"
#include "FileListModel.h"
#include "CreateArchiveDialog.h"
#include "icons.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "ProgressInfo.h"
#include "PowerManager.h"

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

#include <atomic>
#include <thread>
#ifdef Q_OS_MACOS
#include "dnd/MacPromiseDrag.h"
#endif
#include <QDropEvent>
#include <QFileOpenEvent>
#include <QUrl>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QPushButton>
#include <QHeaderView>
#include <QDateTime>
#include <QProgressBar>
#include <QProgressDialog>
#include <QInputDialog>
#include <QTextEdit>
#include <QFontDatabase>
#include <QPixmap>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QActionGroup>
#include <QSettings>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEntry.h"
#include "engine/Bit7zEngine.h"

#include "version.h"

#include <filesystem>
#include <algorithm>
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
    onCloseArchive();
    delete m_icons;
}

// ── UI Setup ───────────────────────────────────────────────────────────
void MainWindow::setupMenus()
{
    // File
    auto fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Archive...\tCtrl+N"), this, &MainWindow::onNewArchive);
    fileMenu->addAction(tr("&Open Archive...\tCtrl+O"), this, &MainWindow::onOpenArchive);
    fileMenu->addAction(tr("&Close Archive\tCtrl+C"), this, &MainWindow::onCloseArchive);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xtract to...\tCtrl+E"), this, &MainWindow::onExtractAll);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit\tAlt+F4"), this, &QMainWindow::close);

    // Commands
    auto cmdMenu = menuBar()->addMenu(tr("&Commands"));
    cmdMenu->addAction(tr("&Add Files...\tAlt+A"), this, &MainWindow::onAddFiles);
    cmdMenu->addAction(tr("Add Fol&der...\tAlt+D"), this, &MainWindow::onAddFolder);
    cmdMenu->addAction(tr("E&xtract...\tAlt+E"), this, &MainWindow::onExtractAll);
    cmdMenu->addAction(tr("&Test\tAlt+T"), this, &MainWindow::onTest);
    cmdMenu->addAction(tr("&View\tAlt+V"), this, &MainWindow::onView);
    cmdMenu->addAction(tr("&Delete\tDel"), this, &MainWindow::onDelete);
    cmdMenu->addSeparator();
    cmdMenu->addAction(tr("&Find...\tF3"), this, [this]() {
        m_searchBox->setVisible(!m_searchBox->isVisible());
        if (m_searchBox->isVisible()) m_searchBox->setFocus();
        else m_model->setFilterString(QString());
    });

    cmdMenu->addAction(tr("&Information...\tCtrl+I"), this, &MainWindow::onInfo);
    cmdMenu->addAction(tr("Archive &Comment..."), this, &MainWindow::onArchiveComment);

    // Options
    auto optsMenu = menuBar()->addMenu(tr("&Options"));
    QAction* flatAction = optsMenu->addAction(tr("&Flat File List"));
    flatAction->setCheckable(true);
    connect(flatAction, &QAction::toggled, this, [this](bool checked) {
        m_model->setFlatMode(checked);
        m_upBtn->setVisible(checked);
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
}

void MainWindow::setupToolbar()
{
    m_toolbar = addToolBar(tr("Tools"));
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto addAct = m_toolbar->addAction(m_icons->add, tr("Add"));
    connect(addAct, &QAction::triggered, this, &MainWindow::onAddFiles);

    auto addDirAct = m_toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder), tr("Add Folder"));
    connect(addDirAct, &QAction::triggered, this, &MainWindow::onAddFolder);

    auto extractAct = m_toolbar->addAction(m_icons->extract, tr("Extract To"));
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
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(m_treeView, &QTreeView::doubleClicked, this, &MainWindow::onItemDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_model, &FileListModel::directoryChanged, this, [this](const QString& dir) {
        m_addrBox->setEditText(dir);
    });

    m_mainLayout->addWidget(m_treeView, 1);

    setCentralWidget(m_centralWidget);
}

// ── Save helpers ───────────────────────────────────────────────────────
bool MainWindow::saveWithProgress()
{
    m_progressDlg->setLabelText(tr("Saving..."));
    m_progressDlg->setRange(0, 0);
    QApplication::processEvents();

    std::atomic<bool> saveDone{false};
    std::atomic<bool> saveOk{false};
    std::mutex spMutex;
    ArchiveEngine::SaveProgressInfo spInfo;
    uint64_t prevBytes = 0;
    ProgressInfo savePi;

    m_engine->setSaveProgressCb([&](const ArchiveEngine::SaveProgressInfo& info) {
        std::lock_guard<std::mutex> lock(spMutex);
        spInfo = info;
    });

    m_engine->resetSaveCancel();

    std::thread saveThread([this, &saveDone, &saveOk]() {
        saveOk = m_engine->Save();
        saveDone = true;
    });

    bool userCancelled = false;

    while (!saveDone)
    {
        QApplication::processEvents();
        QThread::msleep(16);

        if (!userCancelled && m_progressDlg->wasCanceled())
        {
            m_engine->cancelSave();
            userCancelled = true;
            m_progressDlg->setLabelText(tr("Cancelling..."));
        }

        bool needsRangeInit = false;
        bool needsUpdate = false;
        int pct = 0;
        QString fileName;
        QString eta;

        {
            std::lock_guard<std::mutex> lock(spMutex);
            if (spInfo.totalBytes > 0)
            {
                if (savePi.totalBytes == 0)
                {
                    savePi.start(spInfo.totalBytes);
                    needsRangeInit = true;
                }
                savePi.addBytes(spInfo.bytesProcessed - prevBytes);
                prevBytes = spInfo.bytesProcessed;
                if (savePi.shouldUpdate() && !userCancelled)
                {
                    savePi.updateRate();
                    pct = savePi.percent();
                    fileName = QString::fromStdString(spInfo.fileName);
                    eta = savePi.etaString();
                    needsUpdate = true;
                }
            }
        }

        if (needsRangeInit)
        {
            m_progressDlg->setRange(0, 100);
            if (auto* lbl = m_progressDlg->findChild<QLabel*>())
                lbl->setAlignment(Qt::AlignLeft);
        }
        if (needsUpdate)
        {
            m_progressDlg->setValue(pct);
            QString label = fileName + QChar('\n')
                + tr("%1%").arg(pct)
                + (eta.isEmpty() ? QString() : QChar('\n') + eta);
            m_progressDlg->setLabelText(label);
        }
    }

    if (saveThread.joinable())
        saveThread.join();

    m_engine->setSaveProgressCb(nullptr);

    m_progressDlg->close();
    delete m_progressDlg;
    m_progressDlg = nullptr;

    if (m_engine->isSaveCancelled())
    {
        statusBar()->showMessage(tr("Save cancelled."), 3000);
        return false;
    }

    if (!saveOk)
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to save archive."));
        return false;
    }

    return true;
}

bool MainWindow::runSave(const QString& label)
{
    QDialog dlg(this, Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    dlg.setWindowTitle(label.isEmpty() ? tr("Saving...") : label);
    dlg.setFixedSize(300, 80);
    auto* lay = new QVBoxLayout(&dlg);
    auto* lbl = new QLabel(label.isEmpty() ? tr("Saving...") : label, &dlg);
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);
    auto* bar = new QProgressBar(&dlg);
    bar->setRange(0, 0);
    lay->addWidget(bar);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.show();
    QApplication::processEvents();

    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};

    m_engine->resetSaveCancel();

    std::thread t([this, &done, &ok]() {
        ok = m_engine->Save();
        done = true;
    });

    while (!done)
        QApplication::processEvents(QEventLoop::AllEvents, 16);

    if (t.joinable()) t.join();

    if (!ok)
        QMessageBox::warning(this, tr("Error"), tr("Failed to save archive."));

    return ok;
}

bool MainWindow::extractFileWithProgress(
    const ArchiveEntry& entry, const QString& destFile,
    ProgressInfo& pi, uint64_t baseBytes)
{
    struct { std::mutex m; ArchiveEngine::ExtractProgressInfo info; } ep;

    m_engine->setExtractProgressCb([&](const ArchiveEngine::ExtractProgressInfo& info) {
        std::lock_guard<std::mutex> lock(ep.m);
        ep.info = info;
    });

    std::atomic<bool> extractDone{false};
    std::atomic<bool> extractOk{false};
    std::string entryPath = entry.path;
    std::string destStr = destFile.toStdString();

    std::thread extractThread([&]() {
        extractOk = m_engine->Extract(entryPath, destStr);
        extractDone = true;
    });

    QString name = QString::fromUtf8(entry.name.c_str());

    while (!extractDone)
    {
        QApplication::processEvents();
        QThread::msleep(16);

        if (m_progressDlg && m_progressDlg->wasCanceled())
            m_engine->cancelExtract();

        uint64_t fileBytesNow = 0;
        {
            std::lock_guard<std::mutex> lock(ep.m);
            fileBytesNow = ep.info.bytesProcessed;
        }

        pi.bytesProcessed = baseBytes + fileBytesNow;
        if (pi.shouldUpdate())
        {
            pi.updateRate();
            int pct = pi.percent();
            QString eta = pi.etaString();
            if (m_progressDlg)
            {
                m_progressDlg->setValue(pct);
                QString label = name + QChar('\n')
                    + tr("%1%").arg(pct)
                    + (eta.isEmpty() ? QString() : QChar('\n') + eta);
                m_progressDlg->setLabelText(label);
            }
        }
    }

    if (extractThread.joinable())
        extractThread.join();

    m_engine->setExtractProgressCb(nullptr);
    return extractOk;
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
                for (const auto& de : fs::recursive_directory_iterator(src))
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

        if (!saveWithProgress())
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
    if (!engine || !engine->Open(firstVolPath))
    {
        QMessageBox::warning(this, tr("Open Failed"),
            tr("Could not open the archive."));
        return false;
    }

    m_engine = std::move(engine);
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
            for (const auto& de : fs::recursive_directory_iterator(src))
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

    if (!saveWithProgress())
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

    QString dest = QFileDialog::getExistingDirectory(this, tr("Extract to"));
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

void MainWindow::doExtract(const QString& destPath, bool all)
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
            for (const auto& e : entries)
                if (e.path == sp || e.path == sp + "/")
                    { toExtract.push_back(e); break; }
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

    for (size_t i = 0; i < toExtract.size(); ++i)
    {
        if (m_progressDlg->wasCanceled())
        {
            m_extractCancelled = true;
            break;
        }

        const auto& entry = toExtract[i];
        QString name = QString::fromUtf8(entry.name.c_str());

        QString destFile = destPath + "/" + QString::fromUtf8(entry.path.c_str());

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
            QDir().mkpath(destFile);
            continue;
        }

        QDir().mkpath(QFileInfo(destFile).path());

        bool extractOk = extractFileWithProgress(entry, destFile, pi, bytesSoFar);

        bytesSoFar += entry.size;
        pi.bytesProcessed = bytesSoFar;

        if (!extractOk)
        {
            if (m_extractCancelled)
            {
                statusBar()->showMessage(tr("Extraction cancelled."), 3000);
                break;
            }
            qWarning("Failed to extract: %s", entry.path.c_str());
        }
    }

    m_progressDlg->close();
    delete m_progressDlg;
    m_progressDlg = nullptr;

    statusBar()->showMessage(tr("Extraction complete"), 3000);
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
        QMessageBox::warning(this, tr("Info"), tr("Could not read file."));
        return;
    }

    // For images use the actual loaded size; for others use entry metadata
    uint64_t displaySize = imgExts.contains(ext) ? static_cast<uint64_t>(data.size()) : entrySize;

    auto dlg = new QDialog(this);
    dlg->setWindowTitle(tr("View: %1").arg(name));
    dlg->resize(700, 500);
    auto* layout = new QVBoxLayout(dlg);

    auto* infoLabel = new QLabel(tr("%1  —  %2 bytes").arg(name).arg(displaySize), dlg);
    layout->addWidget(infoLabel);

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

            auto fitImage = [view, pixItem]() {
                view->fitInView(pixItem->sceneBoundingRect(), Qt::KeepAspectRatio);
            };
            fitImage();
            dlg->installEventFilter(new ResizeEventFilter(fitImage));
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
            QString content = QString::fromUtf8(
                reinterpret_cast<const char*>(data.data()),
                static_cast<int>(data.size()));
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
    dlg->show();
}

void MainWindow::onInfo()
{
    if (!m_engine) return;

    auto entries = m_engine->ListContents();
    if (entries.empty())
    {
        QMessageBox::information(this, tr("Info"), tr("Archive is empty."));
        return;
    }

    uint64_t totalSize = 0, totalPacked = 0;
    int files = 0, folders = 0;
    for (const auto& e : entries)
    {
        if (e.isDirectory) { folders++; continue; }
        files++;
        totalSize += e.size;
        totalPacked += e.packedSize;
    }

    QString text;
    text += tr("Archive: %1\n").arg(QString::fromStdString(m_currentPath));
    text += tr("Format: %1\n").arg(QString::fromUtf8(m_engine->FormatName().data()));
    text += tr("Files: %1\n").arg(files);
    text += tr("Folders: %1\n").arg(folders);
    text += tr("Total size: %1 bytes\n").arg(totalSize);
    text += tr("Packed size: %1 bytes\n").arg(totalPacked);
    if (totalSize > 0)
    {
        int ratio = static_cast<int>(totalPacked * 100 / totalSize);
        text += tr("Compression ratio: %1%\n").arg(ratio);
    }

    std::string comment = m_engine->archiveComment();
    if (!comment.empty())
        text += tr("\nComment:\n%1\n").arg(QString::fromUtf8(comment.c_str()));

    QMessageBox::information(this, tr("Archive Information"), text);
}

void MainWindow::onArchiveComment()
{
    if (!m_engine) return;

    std::string current = m_engine->archiveComment();
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this, tr("Archive Comment"),
        tr("Comment:"), QString::fromUtf8(current.c_str()), &ok);
    if (!ok) return;

    if (m_engine->setArchiveComment(text.toStdString()))
    {
        runSave(tr("Updating comment..."));
        statusBar()->showMessage(tr("Archive comment updated."), 3000);
    }
    else
    {
        QMessageBox::warning(this, tr("Error"),
            tr("This format does not support archive comments."));
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
        runSave(tr("Updating comment..."));
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

    runSave(tr("Deleting..."));
    refreshFileList();
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
    bool hasEngine = (m_engine != nullptr);
    bool hasSelection = !m_treeView->selectionModel()->selectedRows(0).isEmpty();

    QMenu menu(this);

    QAction* extractAct = menu.addAction(tr("Extract..."), this, [this, hasSelection]() {
        auto sel = m_treeView->selectionModel()->selectedRows(0);
        if (hasSelection)
            doExtractSelected(sel);
        else
            onExtractAll();
    });
    extractAct->setEnabled(hasEngine);

    QAction* viewAct = menu.addAction(tr("View"), this, &MainWindow::onView);
    viewAct->setEnabled(hasEngine && hasSelection);

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
    {
        vfdo->AddRef();
        StartVirtualDrag(vfdo, (HWND)winId());
    }
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
    // Extract to temp, then QDrag with file URLs
    QString tmpRoot = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + "/ZipFX_Drag/"
        + QString::number(QDateTime::currentSecsSinceEpoch()) + "/";
    QDir().mkpath(tmpRoot);

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
        drag->exec(Qt::CopyAction);
    }
#endif
#endif
}

// ── macOS: handle "Open with" / double-click in Finder ─────────────
bool MainWindow::event(QEvent* event)
{
    if (event->type() == QEvent::FileOpen)
    {
        auto* foe = static_cast<QFileOpenEvent*>(event);
        if (foe && !foe->file().isEmpty())
            openArchive(foe->file());
        return true;
    }
    return QMainWindow::event(event);
}

// ── Drag & Drop ────────────────────────────────────────────────────────
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
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
    m_treeView->setColumnHidden(FileListModel::ColPacked, !hasPacked);
    m_treeView->setColumnHidden(FileListModel::ColCRC, !hasCRC);
    m_treeView->setColumnHidden(FileListModel::ColPermissions, !hasPerms);
    m_treeView->setColumnHidden(FileListModel::ColComment, !hasComment);

    statusBar()->showMessage(
        tr("%1 files").arg(entries.size()));
    setWindowTitle(tr("ZipFX — %1").arg(
        QString::fromStdString(m_currentPath)));
}

// ── Windows: register file associations on first launch ────────────
#ifdef _WIN32
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
    QStringList exts = {
        ".zip", ".7z", ".rar", ".tar", ".gz", ".gzip", ".bz2", ".bzip2",
        ".xz", ".tgz", ".tbz2", ".txz", ".iso", ".cab", ".arj", ".lz",
        ".lzma", ".rpm", ".deb", ".msi", ".wim", ".vhd", ".vmdk"
    };

    for (const QString& ext : exts)
    {
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
#endif
