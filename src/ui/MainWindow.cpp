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

#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEntry.h"

#include <algorithm>

#ifdef _WIN32
#include "dnd/VirtualFileDataObject.h"
#endif

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

    auto extractAct = m_toolbar->addAction(m_icons->extract, tr("Extract To"));
    connect(extractAct, &QAction::triggered, this, &MainWindow::onExtractAll);

    auto testAct = m_toolbar->addAction(m_icons->test, tr("Test"));
    connect(testAct, &QAction::triggered, this, &MainWindow::onTest);

    auto viewAct = m_toolbar->addAction(m_icons->view, tr("View"));
    connect(viewAct, &QAction::triggered, this, &MainWindow::onView);

    m_toolbar->addSeparator();

    auto delAct = m_toolbar->addAction(m_icons->del, tr("Delete"));
    connect(delAct, &QAction::triggered, this, &MainWindow::onDelete);

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
    auto central = new QWidget(this);
    auto layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

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
    layout->addLayout(addrLayout);

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
    m_treeView->setSortingEnabled(false);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(m_treeView, &QTreeView::doubleClicked, this, &MainWindow::onItemDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_model, &FileListModel::directoryChanged, this, [this](const QString& dir) {
        m_addrBox->setEditText(dir);
    });

    layout->addWidget(m_treeView, 1);

    setCentralWidget(central);
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
    m_currentPath = result.path.toStdString();
    m_addrBox->setEditText(result.path);
    statusBar()->showMessage(tr("Archive created"), 3000);
    refreshFileList();
}

void MainWindow::onOpenArchive()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open Archive"), "",
        tr("Supported Archives (*.zip *.7z *.rar *.tar *.tgz *.tar.gz);;"
           "ZIP (*.zip);;7z (*.7z);;RAR (*.rar);;"
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
    if (files.isEmpty()) return;

    QString prefix = m_model->currentDir();
    if (!prefix.isEmpty()) prefix += "/";

    m_progressDlg = new QProgressDialog(tr("Adding files..."), tr("Cancel"),
        0, files.size(), this);
    m_progressDlg->setWindowModality(Qt::WindowModal);
    m_progressDlg->show();

    for (int i = 0; i < files.size(); ++i)
    {
        if (m_progressDlg->wasCanceled()) break;
        m_progressDlg->setValue(i);
        m_progressDlg->setLabelText(tr("Adding: %1").arg(QFileInfo(files[i]).fileName()));
        QApplication::processEvents();

        QFileInfo fi(files[i]);
        QString archivePath = prefix + fi.fileName();
        m_engine->AddFile(files[i].toStdString(), archivePath.toStdString());
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
    m_progressDlg->setWindowModality(Qt::WindowModal);
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
    if (m_engine->TestIntegrity())
        QMessageBox::information(this, tr("Test"), tr("Integrity check passed."));
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
    QMenu menu(this);

    menu.addAction(tr("Extract..."), this, [this]() {
        auto sel = m_treeView->selectionModel()->selectedRows(0);
        if (!sel.isEmpty())
            doExtractSelected(sel);
        else
            onExtractAll();
    });

    menu.addAction(tr("View"), this, &MainWindow::onView);

    if (m_engine && m_engine->SupportsCreation())
        menu.addAction(tr("Delete"), this, &MainWindow::onDelete);

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

#ifdef _WIN32
    // Windows: VirtualFileDataObject — preserves structure
    VirtualFileDataObject* vfdo = new VirtualFileDataObject();
    for (const auto& fp : filePaths)
    {
        VirtualFileEntry ve;
        std::string fullPath = fp.toStdString();
        ve.name = QString::fromUtf8(fullPath.c_str()).toStdWString();
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
    prog.setWindowModality(Qt::WindowModal);
    prog.show();

    QList<QUrl> urls;
    for (int i = 0; i < filePaths.size(); ++i)
    {
        if (prog.wasCanceled()) break;
        prog.setValue(i);
        prog.setLabelText(tr("Extracting: %1").arg(filePaths[i]));
        QApplication::processEvents();

        std::string fp = filePaths[i].toStdString();
        QString destPath = tmpRoot + filePaths[i];
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
        QFileInfo fi(path);
        QString archivePath = prefix + fi.fileName();
        m_engine->AddFile(path.toStdString(), archivePath.toStdString());
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
