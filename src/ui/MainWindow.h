#ifndef ZIPFX_MAINWINDOW_H
#define ZIPFX_MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QProgressDialog>
#include <QTranslator>

#include <memory>

class ArchiveEngine;
class FileListModel;
struct ZipFXIcons;

// Custom tree view that intercepts drag to use VirtualFileDataObject on Windows
class ArchiveTreeView : public QTreeView
{
    Q_OBJECT
public:
    explicit ArchiveTreeView(QWidget* parent = nullptr) : QTreeView(parent) {}
    ~ArchiveTreeView() override = default;

signals:
    void dragStarted();

protected:
    void startDrag(Qt::DropActions) override
    {
        emit dragStarted();
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(const QString& fileToOpen, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool event(QEvent* event) override;

private slots:
    void onNewArchive();
    void onOpenArchive();
    void onCloseArchive();
    void onAddFiles();
    void onAddFolder();
    void onExtractAll();
    void onExtractSelected();
    void onTest();
    void onView();
    void onInfo();
    void onDelete();

    void onItemDoubleClicked(const QModelIndex& index);
    void onContextMenu(const QPoint& pos);

    void onBeginDrag();

private:
    void setupMenus();
    void setupToolbar();
    void setupUI();
    void refreshFileList();
    void updateStatusBar();
    bool openArchive(const QString& path);
#ifdef _WIN32
    void registerFileAssociations();
#endif
    void doExtract(const QString& destPath, bool all);
    void doExtractSelected(const QModelIndexList& selection);
    void doAddPaths(const QStringList& paths);

    std::unique_ptr<ArchiveEngine> m_engine;
    std::string m_currentPath;

    // UI
    QWidget*      m_centralWidget = nullptr;
    QVBoxLayout*  m_mainLayout = nullptr;
    ArchiveTreeView* m_treeView = nullptr;
    FileListModel*    m_model = nullptr;
    QComboBox*    m_addrBox = nullptr;
    QPushButton*  m_upBtn = nullptr;
    QToolBar*     m_toolbar = nullptr;
    QLineEdit*    m_searchBox = nullptr;
    ZipFXIcons*   m_icons = nullptr;

    // Translation
    QTranslator* m_currentTranslator = nullptr;

    // Extraction
    QProgressDialog* m_progressDlg = nullptr;
    bool m_extractCancelled = false;
};

#endif
