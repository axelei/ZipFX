#ifndef ZIPFX_FILE_LIST_MODEL_H
#define ZIPFX_FILE_LIST_MODEL_H

#include <QAbstractItemModel>
#include <QIcon>
#include <QStringList>
#include <vector>

#include "engine/ArchiveEntry.h"

class QFileIconProvider;

class FileListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColSize,
        ColPacked,
        ColType,
        ColModified,
        ColCRC,
        ColCount
    };

    explicit FileListModel(QObject* parent = nullptr);
    ~FileListModel() override;

    void setEntries(const std::vector<ArchiveEntry>& entries);
    void clear();
    bool isFlatMode() const { return m_flatMode; }
    void setFlatMode(bool flat);

    // Filtering
    void setFilterString(const QString& filter);
    QString filterString() const { return m_filterString; }

    // Navigation
    QString currentDir() const { return m_currentDir; }
    void setCurrentDir(const QString& dir);
    void navigateInto(const QString& subdir);
    void navigateUp();

    // Selection helpers
    QStringList selectedEntryPaths(const QModelIndexList& selection) const;

    // QAbstractItemModel
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Item is accessed by MainWindow via internalPointer()
    struct Item
    {
        QString     name;
        QString     fullPath;
        bool        isDir = false;
        bool        isParent = false;
        uint64_t    size = 0;
        uint64_t    packedSize = 0;
        uint32_t    crc = 0;
        time_t      modified = 0;
        bool        hasEntry = false;

        Item*       parentItem = nullptr;
        std::vector<Item*> children;
    };

signals:
    void directoryChanged(const QString& path);

private:
    void rebuild();
    void sortItems();
    QString relativePath(const QString& full) const;
    QIcon iconForFile(const QString& name, bool isDir) const;

    std::vector<ArchiveEntry> m_allEntries;
    Item* m_rootItem = nullptr;
    bool  m_flatMode = false;
    QString m_currentDir;
    QString m_currentDirPrefix;

    // Cache for file type icons
    mutable QFileIconProvider* m_iconProvider = nullptr;

    QString m_filterString;

    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

#endif
