#include "MainWindow.h"
#include "FileListModel.h"
#include "CreateArchiveDialog.h"
#include "icons.h"

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
#include <QDropEvent>
#include <QUrl>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QPushButton>
#include <QHeaderView>
#include <QDateTime>
#include <QProgressDialog>
#include <QActionGroup>
#include <QSettings>

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEntry.h"

#include <filesystem>
#include <algorithm>
namespace fs = std::filesystem;

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
MainWindow::MainWindow()
    : QMainWindow()
{
    setWindowTitle(tr("ZipFX"));
    resize(960, 640);

    m_icons = new ZipFXIcons(CreatePlaceholderIcons());

    setupMenus();
    setupToolbar();
    setupUI();

    // Enable drops on the main window
    setAcceptDrops(true);

    // Status bar
    statusBar()->showMessage(tr("Ready"));
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
    cmdMenu->addAction(tr("&Find...\tF3"));
    cmdMenu->addAction(tr("&Wizard...\tCtrl+W"));
    cmdMenu->addAction(tr("&Information...\tCtrl+I"));

    // Options
    auto optsMenu = menuBar()->addMenu(tr("&Options"));
    QAction* flatAction = optsMenu->addAction(tr("&Flat File List"));
    flatAction->setCheckable(true);
    connect(flatAction, &QAction::toggled, this, [this](bool checked) {
        m_model->setFlatMode(checked);
    });

    optsMenu->addSeparator();
    auto langMenu = optsMenu->addMenu(tr("&Language"));
    QActionGroup* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);

    auto addLang = [&](const QString& name, const QString& locale) {
        QAction* act = langMenu->addAction(name);
        act->setData(locale);
        act->setCheckable(true);
        act->setChecked(locale == QLocale::system().name().left(2));
        langGroup->addAction(act);
    };
    addLang(tr("English"), "en");
    addLang(tr("Spanish"), "es");

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
            tr("ZipFX v1.0\n\nA cross-platform archive manager.\n"
               "Supported: ZIP, 7z, RAR, TAR.GZ"));
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

    auto findAct = m_toolbar->addAction(m_icons->find, tr("Find"));
    Q_UNUSED(findAct);

    m_toolbar->addSeparator();

    auto wizardAct = m_toolbar->addAction(m_icons->wizard, tr("Wizard"));
    Q_UNUSED(wizardAct);

    auto infoAct = m_toolbar->addAction(m_icons->info, tr("Info"));
    Q_UNUSED(infoAct);
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
    auto upBtn = new QPushButton("..", this);
    upBtn->setFixedWidth(28);
    connect(upBtn, &QPushButton::clicked, this, [this]() {
        m_model->navigateUp();
        m_addrBox->setEditText(m_model->currentDir());
    });

    addrLayout->addWidget(addrLabel);
    addrLayout->addWidget(m_addrBox, 1);
    addrLayout->addWidget(upBtn);
    m_mainLayout->addLayout(addrLayout);

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

// ── Archive actions ────────────────────────────────────────────────────
void MainWindow::onNewArchive()
{
    CreateArchiveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto result = dlg.result();
    onCloseArchive();

    auto engine = ArchiveEngineFactory::CreateForFormat(result.format.toStdString());
    if (!engine || !engine->SupportsCreation())
    {
        QMessageBox::warning(this, tr("Error"), tr("Format not supported."));
        return;
    }

    if (!engine->Create(result.path.toStdString()))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not create archive."));
        return;
    }

    m_engine = std::move(engine);

    // Auto-add source items if provided
    if (!result.sourcePaths.isEmpty())
    {
        int total = 0;
        for (const auto& sp : result.sourcePaths)
        {
            fs::path src(sp.toStdWString());
            if (fs::is_directory(src))
            {
                for (const auto& de : fs::recursive_directory_iterator(src))
                    if (de.is_regular_file()) total++;
            }
            else if (fs::is_regular_file(src))
            {
                total++;
            }
        }

        m_progressDlg = new QProgressDialog(tr("Adding files..."), nullptr,
            0, total, this);
        m_progressDlg->setWindowModality(Qt::ApplicationModal);
        m_progressDlg->show();

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
                        QApplication::processEvents();
                    }
                }
            }
            else if (fs::is_regular_file(src))
            {
                m_engine->AddFile(src.string(), src.filename().string());
                m_progressDlg->setValue(++count);
                QApplication::processEvents();
            }
        }

        m_progressDlg->setLabelText(tr("Saving..."));
        QApplication::processEvents();

        if (!m_engine->Save())
        {
            m_progressDlg->close();
            delete m_progressDlg;
            m_progressDlg = nullptr;
            QMessageBox::warning(this, tr("Error"), tr("Failed to save archive."));
            return;
        }

        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
    }

    m_currentPath = result.path.toStdString();
    m_addrBox->setEditText(result.path);
    m_treeView->setEnabled(true);
    statusBar()->showMessage(tr("Archive created"), 3000);
    refreshFileList();
}

void MainWindow::onOpenArchive()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open Archive"), "",
        tr("Supported Archives (*.zip *.7z *.rar *.iso *.cab *.lzh *.lha *.xar *.cpio *.tar *.tgz *.tar.gz);;"
           "ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;ISO (*.iso);;CAB (*.cab);;"
           "LHA (*.lzh *.lha);;XAR (*.xar);;CPIO (*.cpio);;"
           "TAR (*.tar *.tgz *.tar.gz);;All (*.*)"));
    if (path.isEmpty()) return;

    openArchive(path);
}

bool MainWindow::openArchive(const QString& path)
{
    onCloseArchive();

    std::string spath = path.toStdString();
    auto engine = ArchiveEngineFactory::CreateForFile(spath);
    if (!engine || !engine->Open(spath))
    {
        QMessageBox::warning(this, tr("Open Failed"),
            tr("Could not open the archive."));
        return false;
    }

    m_engine = std::move(engine);
    m_currentPath = spath;
    m_addrBox->setEditText(path);
    m_treeView->setEnabled(true);
    statusBar()->showMessage(tr("Opened: %1").arg(path), 3000);
    refreshFileList();
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

    // Count total files (recursing directories)
    int total = 0;
    for (const auto& path : paths)
    {
        fs::path src(path.toStdWString());
        if (fs::is_directory(src))
        {
            for (const auto& de : fs::recursive_directory_iterator(src))
                if (de.is_regular_file()) total++;
        }
        else if (fs::is_regular_file(src))
        {
            total++;
        }
    }

    if (total == 0) return;

    m_progressDlg = new QProgressDialog(tr("Adding files..."), tr("Cancel"),
        0, total, this);
    m_progressDlg->setWindowModality(Qt::ApplicationModal);
    m_progressDlg->show();

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
                    m_progressDlg->setLabelText(tr("Adding: %1").arg(
                        QString::fromStdWString(rel.wstring())));
                    QApplication::processEvents();
                }
            }
        }
        else if (fs::is_regular_file(src))
        {
            m_engine->AddFile(path.toStdString(), archiveBase.toStdString());
            m_progressDlg->setValue(++count);
            m_progressDlg->setLabelText(tr("Adding: %1").arg(
                QString::fromStdWString(src.filename().wstring())));
            QApplication::processEvents();
        }
    }

    m_progressDlg->setLabelText(tr("Saving..."));
    QApplication::processEvents();

    if (!m_engine->Save())
        QMessageBox::warning(this, tr("Error"), tr("Failed to save archive."));

    m_progressDlg->close();
    delete m_progressDlg;
    m_progressDlg = nullptr;

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

    m_progressDlg = new QProgressDialog(tr("Extracting..."), tr("Cancel"),
        0, (int)toExtract.size(), this);
    m_progressDlg->setWindowModality(Qt::ApplicationModal);
    m_progressDlg->show();

    bool applyToAll = false;

    for (size_t i = 0; i < toExtract.size(); ++i)
    {
        if (m_progressDlg->wasCanceled())
        {
            m_extractCancelled = true;
            break;
        }

        const auto& entry = toExtract[i];
        QString name = QString::fromUtf8(entry.name.c_str());
        m_progressDlg->setValue((int)i);
        m_progressDlg->setLabelText(tr("Extracting: %1").arg(name));
        QApplication::processEvents();

        QString destFile = destPath + "/" + QString::fromUtf8(entry.path.c_str());

        // Overwrite check
        if (QFileInfo::exists(destFile) && !applyToAll)
        {
            // Simplified overwrite: use QMessageBox
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

        if (!m_engine->Extract(entry.path, destFile.toStdString()))
            qWarning("Failed to extract: %s", entry.path.c_str());
    }

    m_progressDlg->close();
    delete m_progressDlg;
    m_progressDlg = nullptr;

    statusBar()->showMessage(tr("Extraction complete"), 3000);
}

void MainWindow::doExtractSelected(const QModelIndexList& selection)
{
    QString dest = QFileDialog::getExistingDirectory(this, tr("Extract selected to"));
    if (dest.isEmpty()) return;

    auto paths = m_model->selectedEntryPaths(selection);
    auto allEntries = m_engine->ListContents();
    std::vector<ArchiveEntry> toExtract;

    for (const auto& p : paths)
    {
        std::string sp = p.toStdString();
        bool isDir = false;
        for (const auto& e : allEntries)
            if (e.path == sp || e.path == sp + "/")
                { isDir = e.isDirectory; break; }

        if (isDir)
        {
            std::string prefix = sp + "/";
            for (const auto& e : allEntries)
                if (!e.isDirectory && e.path.compare(0, prefix.size(), prefix) == 0)
                    toExtract.push_back(e);
        }
        else
        {
            for (const auto& e : allEntries)
                if (e.path == sp) { toExtract.push_back(e); break; }
        }
    }

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

    QProgressDialog prog(tr("Testing integrity..."), tr("Cancel"),
        0, total, this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.show();

    bool result = m_engine->TestIntegrity(
        [&](int current, int) {
            prog.setValue(current);
            if (current < total)
            {
                prog.setLabelText(tr("Testing: %1").arg(
                    QString::fromUtf8(entries[current].name.c_str())));
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

    auto data = m_engine->ReadFile(paths[0].toStdString());
    if (data.empty())
    {
        QMessageBox::warning(this, tr("Info"), tr("Could not read file."));
        return;
    }
    QMessageBox::information(this, tr("File Info"),
        tr("File: %1\nSize: %2 bytes").arg(paths[0]).arg(data.size()));
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

    m_engine->Save();
    refreshFileList();
}

// ── List interactions ──────────────────────────────────────────────────
void MainWindow::onItemDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    auto* model = static_cast<FileListModel*>(m_treeView->model());

    if (model->isFlatMode()) return;

    auto* item = static_cast<FileListModel::Item*>(index.internalPointer());
    if (!item) return;

    if (item->name == "..")
    {
        model->navigateUp();
    }
    else if (item->isDir)
    {
        model->navigateInto(item->name);
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
    // Extract to temp, then QDrag with file URLs
    QString tmpRoot = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + "/ZipFX_Drag/"
        + QString::number(QDateTime::currentSecsSinceEpoch()) + "/";
    QDir().mkpath(tmpRoot);

    QProgressDialog prog(tr("Preparing files for drag..."), tr("Cancel"),
        0, filePaths.size(), this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.show();

    QList<QUrl> urls;
    for (int i = 0; i < filePaths.size(); ++i)
    {
        if (prog.wasCanceled()) break;
        prog.setValue(i);
        prog.setLabelText(tr("Extracting: %1").arg(filePaths[i]));
        QApplication::processEvents();

        std::string fp = filePaths[i].toStdString();
        QString dragName = fp.startsWith(prefix) ? fp.mid(prefix.size()) : fp;
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

    QString prefix = m_model->currentDir();
    if (!prefix.isEmpty()) prefix += "/";

    for (const auto& path : paths)
    {
        fs::path src(path.toStdWString());
        QString archiveBase = prefix + QString::fromStdWString(src.filename().wstring());

        if (fs::is_directory(src))
        {
            // Recurse into directory and add every file inside
            for (const auto& de : fs::recursive_directory_iterator(src))
            {
                if (de.is_regular_file())
                {
                    fs::path rel = de.path().lexically_relative(src);
                    QString archivePath = archiveBase + "/"
                        + QString::fromStdWString(rel.generic_wstring());
                    m_engine->AddFile(de.path().string(), archivePath.toStdString());
                }
            }
        }
        else if (fs::is_regular_file(src))
        {
            m_engine->AddFile(path.toStdString(), archiveBase.toStdString());
        }
    }

    if (!m_engine->Save())
        QMessageBox::warning(this, tr("Error"), tr("Failed to save."));

    refreshFileList();
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

    statusBar()->showMessage(
        tr("%1 files").arg(entries.size()));
    setWindowTitle(tr("ZipFX — %1").arg(
        QString::fromStdString(m_currentPath)));
}
