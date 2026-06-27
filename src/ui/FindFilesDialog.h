#ifndef ZIPFX_FIND_FILES_DIALOG_H
#define ZIPFX_FIND_FILES_DIALOG_H

#include <QDialog>
#include <vector>

#include "engine/ArchiveEntry.h"

class QLineEdit;
class QSpinBox;
class QDateEdit;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;

class FindFilesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FindFilesDialog(std::vector<ArchiveEntry> entries, QWidget* parent = nullptr);

signals:
    void entryActivated(const QString& path);
    void entryExtractRequested(const QString& path);

private:
    void doSearch();
    void onItemActivated(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);

    std::vector<ArchiveEntry> m_entries;
    QLineEdit*   m_nameEdit     = nullptr;
    QCheckBox*   m_useSizeRange = nullptr;
    QSpinBox*    m_minSizeSpin  = nullptr;
    QSpinBox*    m_maxSizeSpin  = nullptr;
    QCheckBox*   m_useDateRange = nullptr;
    QDateEdit*   m_fromDate     = nullptr;
    QDateEdit*   m_toDate       = nullptr;
    QTreeWidget* m_resultsTree  = nullptr;
};

#endif
