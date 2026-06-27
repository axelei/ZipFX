#ifndef ZIPFX_MAINWINDOW_H
#define ZIPFX_MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QProgressDialog>
#include <QStringList>
#include <QTranslator>

class QSplitter;
class QStackedWidget;
class QGraphicsView;
class QGraphicsScene;
class QTextEdit;

#include "ProgressInfo.h"

#include <memory>
#include <set>

class ArchiveEngine;
struct ArchiveEntry;
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
    void backspacePressed();

protected:
    void startDrag(Qt::DropActions) override
    {
        emit dragStarted();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Backspace) {
            emit backspacePressed();
        } else {
            QTreeView::keyPressEvent(event);
        }
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(const QString& fileToOpen, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openArchive(const QString& path);
    void shellAdd(const QStringList& files);

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
    void onArchiveComment();
    void onEntryComment();

    void onItemDoubleClicked(const QModelIndex& index);
    void onContextMenu(const QPoint& pos);

    void onBeginDrag();

private:
    void setupMenus();
    void setupToolbar();
    void setupUI();
    void refreshFileList();
    void updateStatusBar();
    void updatePreview();
    void onOpenEntry(bool pickApp);
    void onConvertArchive();
    void onRepairArchive();
    void onBatchOps();
    void onPasswordManager();
    void onFindFiles();
    void savePassword(const QString& archive, const QString& password);
    QString loadPassword(const QString& archive);
    void onAddRecoveryRecord();
    void onVerifyRecoveryRecord();
    void onChecksums();
    void installShellExtension(bool install);
#ifdef _WIN32
    void registerFileAssociations();
#elif defined(__APPLE__)
    void registerFileAssociationsMac();
#endif
    void doExtract(const QString& destPath, bool all, bool stripPaths = false);
    void doExtractSelected(const QModelIndexList& selection);
    void doAddPaths(const QStringList& paths);
    bool saveWithProgress();
    bool runSave(const QString& label = {});
    bool extractFileWithProgress(const ArchiveEntry& entry, const QString& destFile,
                                 ProgressInfo& pi, uint64_t baseBytes);
    void afterActionDialog();
    void loadRecentFiles();
    void addRecentFile(const QString& path);

    std::unique_ptr<ArchiveEngine> m_engine;
    std::string m_currentPath;
    std::string m_archivePassword;
    QStringList m_excludePatterns;
    bool m_keepBrokenFiles = false;
    std::set<int> m_userManagedCols;

    // UI
    QWidget*         m_centralWidget  = nullptr;
    QVBoxLayout*     m_mainLayout     = nullptr;
    QSplitter*       m_splitter       = nullptr;
    ArchiveTreeView* m_treeView       = nullptr;
    FileListModel*   m_model          = nullptr;
    QComboBox*       m_addrBox        = nullptr;
    QPushButton*     m_upBtn          = nullptr;
    QToolBar*        m_toolbar        = nullptr;
    QLineEdit*       m_searchBox      = nullptr;
    ZipFXIcons*      m_icons          = nullptr;

    // Preview pane
    QWidget*         m_previewPanel   = nullptr;
    QLabel*          m_previewInfo    = nullptr;
    QStackedWidget*  m_previewStack   = nullptr;
    QTextEdit*       m_previewText    = nullptr;
    QGraphicsView*   m_previewImage   = nullptr;
    QGraphicsScene*  m_previewScene   = nullptr;

    // Translation
    QTranslator* m_currentTranslator = nullptr;

    // Menu actions that need dynamic enable/disable
    QAction* m_archiveCommentAct = nullptr;
    QAction* m_flatAct = nullptr;
    bool     m_openAfterExtract = false;

    // Extraction
    QProgressDialog* m_progressDlg = nullptr;
    bool m_extractCancelled = false;
};

#endif
