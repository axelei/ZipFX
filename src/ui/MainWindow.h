#ifndef ZIPFX_MAINWINDOW_H
#define ZIPFX_MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QComboBox>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QProgressDialog>

#include <memory>

class ArchiveEngine;
class FileListModel;
struct ZipFXIcons;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onNewArchive();
    void onOpenArchive();
    void onCloseArchive();
    void onAddFiles();
    void onExtractAll();
    void onExtractSelected();
    void onTest();
    void onView();
    void onDelete();

    void onItemDoubleClicked(const QModelIndex& index);
    void onContextMenu(const QPoint& pos);

    void onBeginDrag();
    void onExtractFinished();

private:
    void setupMenus();
    void setupToolbar();
    void setupUI();
    void refreshFileList();
    void updateStatusBar();
    bool openArchive(const QString& path);
    void doExtract(const QString& destPath, bool all);
    void doExtractSelected(const QModelIndexList& selection);

    std::unique_ptr<ArchiveEngine> m_engine;
    std::string m_currentPath;

    // UI
    QTreeView*    m_treeView = nullptr;
    FileListModel* m_model = nullptr;
    QComboBox*    m_addrBox = nullptr;
    QToolBar*     m_toolbar = nullptr;
    ZipFXIcons*   m_icons = nullptr;

    // Extraction
    QProgressDialog* m_progressDlg = nullptr;
    bool m_extractCancelled = false;
};

#endif
