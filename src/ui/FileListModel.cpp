#include "FileListModel.h"

#include <QDateTime>
#include <QFileIconProvider>
#include <QIcon>

#include <algorithm>
#include <set>

static QString fmtSize(uint64_t b)
{
    if (b == 0) return QString();
    if (b < 1024) return QString::number(b) + QLatin1String(" B");
    if (b < 1024*1024) return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < static_cast<uint64_t>(1024)*1024*1024)
        return QString("%1 MB").arg(b / (1024.0*1024), 0, 'f', 2);
    return QString("%1 GB").arg(b / (1024.0*1024*1024), 0, 'f', 2);
}

static QString sanitizeName(const QString& name)
{
    QString safe = name;
    safe.replace('/',  QChar('_'));
    safe.replace('\\', QChar('_'));
    safe.replace(':',  QChar('_'));
    safe.replace('"',  QChar('_'));
    safe.replace('*',  QChar('_'));
    safe.replace('?',  QChar('_'));
    safe.replace('<',  QChar('_'));
    safe.replace('>',  QChar('_'));
    safe.replace('|',  QChar('_'));
    for (int i = 0; i < safe.size(); ++i)
        if (safe[i] < QChar(0x20) || safe[i] == QChar(0x7f))
            safe[i] = QChar('_');
    return safe;
}

FileListModel::FileListModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_rootItem = new Item;
    m_rootItem->name = "/";
    m_rootItem->fullPath = "";
    m_rootItem->isDir = true;
}

FileListModel::~FileListModel()
{
    delete m_rootItem;
    delete m_iconProvider;
}

// ── Data ───────────────────────────────────────────────────────────────
void FileListModel::setEntries(const std::vector<ArchiveEntry>& entries)
{
    m_allEntries = entries;
    rebuild();
}

void FileListModel::clear()
{
    beginResetModel();
    m_allEntries.clear();
    m_currentDir.clear();
    m_currentDirPrefix.clear();
    delete m_rootItem;
    m_rootItem = new Item;
    m_rootItem->name = "/";
    m_rootItem->fullPath = "";
    m_rootItem->isDir = true;
    endResetModel();
}

void FileListModel::setFlatMode(bool flat)
{
    if (m_flatMode == flat) return;
    m_flatMode = flat;
    rebuild();
}

// ── Navigation ─────────────────────────────────────────────────────────
void FileListModel::setCurrentDir(const QString& dir)
{
    m_currentDir = dir;
    if (dir.isEmpty() || dir.endsWith('/'))
        m_currentDirPrefix = dir;
    else
        m_currentDirPrefix = dir + "/";
    rebuild();
}

void FileListModel::navigateInto(const QString& subdir)
{
    QString newDir = m_currentDirPrefix + subdir;
    setCurrentDir(newDir);
    emit directoryChanged(m_currentDir);
}

void FileListModel::navigateUp()
{
    if (m_currentDir.isEmpty()) return;
    int pos = m_currentDir.lastIndexOf('/');
    if (pos < 0)
        setCurrentDir("");
    else
        setCurrentDir(m_currentDir.left(pos));
    emit directoryChanged(m_currentDir);
}

QStringList FileListModel::selectedEntryPaths(
    const QModelIndexList& selection) const
{
    QStringList result;
    for (const auto& idx : selection)
    {
        if (!idx.isValid()) continue;
        auto* item = static_cast<Item*>(idx.internalPointer());
        if (!item || item->isParent) continue;
        result << item->fullPath;
    }
    return result;
}

// ── Filtering ──────────────────────────────────────────────────────────
void FileListModel::setFilterString(const QString& filter)
{
    m_filterString = filter;
    rebuild();
}

// ── Rebuild ────────────────────────────────────────────────────────────
void FileListModel::rebuild()
{
    beginResetModel();

    delete m_rootItem;
    m_rootItem = new Item;
    m_rootItem->name = "/";
    m_rootItem->fullPath = "";
    m_rootItem->isDir = true;

    if (m_allEntries.empty())
    {
        endResetModel();
        return;
    }

    auto matchesFilter = [this](const std::string& name) {
        if (m_filterString.isEmpty()) return true;
        return QString::fromUtf8(name.c_str()).contains(m_filterString, Qt::CaseInsensitive);
    };

    if (m_flatMode)
    {
        for (const auto& e : m_allEntries)
        {
            if (e.name.empty()) continue;
            if (!matchesFilter(e.name)) continue;
            auto* item = new Item;
            item->name = sanitizeName(QString::fromUtf8(e.name.c_str()));
            item->fullPath = QString::fromUtf8(e.path.c_str());
            item->isDir = e.isDirectory;
            item->size = e.size;
            item->packedSize = e.packedSize;
            item->crc = e.crc;
            item->permissions = e.permissions;
            item->modified = std::chrono::system_clock::to_time_t(e.modified);
            item->hasEntry = true;
            item->comment = QString::fromUtf8(e.comment.c_str());
            item->compressionMethod = QString::fromUtf8(e.compressionMethod.c_str());
            item->parentItem = m_rootItem;
            m_rootItem->children.push_back(item);
        }
    }
    else
    {
        if (!m_currentDir.isEmpty())
        {
            auto* parent = new Item;
            parent->name = "..";
            parent->fullPath = m_currentDir;
            parent->isDir = true;
            parent->isParent = true;
            parent->parentItem = m_rootItem;
            m_rootItem->children.push_back(parent);
        }

        std::set<QString> seenDirs;

        for (const auto& e : m_allEntries)
        {
            QString ep = QString::fromUtf8(e.path.c_str());
            if (!ep.startsWith(m_currentDirPrefix))
                continue;

            QString remainder = ep.mid(m_currentDirPrefix.size());
            if (!m_filterString.isEmpty() &&
                !remainder.contains(m_filterString, Qt::CaseInsensitive))
                continue;

            int slash = remainder.indexOf('/');
            bool isDir = slash >= 0 || e.isDirectory;

            if (isDir && slash >= 0)
            {
                QString dirName = remainder.left(slash);
                if (seenDirs.insert(dirName).second)
                {
                    auto* item = new Item;
                    item->name = sanitizeName(dirName);
                    item->fullPath = m_currentDirPrefix + dirName;
                    item->isDir = true;
                    item->parentItem = m_rootItem;
                    m_rootItem->children.push_back(item);
                }
            }
            else if (!e.isDirectory && !remainder.isEmpty())
            {
                auto* item = new Item;
                item->name = sanitizeName(remainder);
                item->fullPath = ep;
                item->isDir = false;
                item->size = e.size;
                item->packedSize = e.packedSize;
                item->crc = e.crc;
                item->permissions = e.permissions;
                item->modified = std::chrono::system_clock::to_time_t(e.modified);
                item->hasEntry = true;
                item->comment = QString::fromUtf8(e.comment.c_str());
                item->compressionMethod = QString::fromUtf8(e.compressionMethod.c_str());
                item->parentItem = m_rootItem;
                m_rootItem->children.push_back(item);
            }
        }
    }

    sortItems();
    endResetModel();
}

// ── QAbstractItemModel ─────────────────────────────────────────────────
int FileListModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return (int)m_rootItem->children.size();
    auto* item = static_cast<Item*>(parent.internalPointer());
    return item ? (int)item->children.size() : 0;
}

int FileListModel::columnCount(const QModelIndex&) const
{
    return ColCount;
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    auto* item = static_cast<Item*>(index.internalPointer());
    if (!item) return {};

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case ColName:     return item->name;
        case ColSize:
            return item->hasEntry ? fmtSize(item->size) : QVariant();
        case ColPacked:
            return (item->hasEntry && item->packedSize > 0 && item->packedSize != item->size)
                ? fmtSize(item->packedSize) : QVariant();
        case ColRatio:
            if (!item->hasEntry || item->isDir || item->size == 0 || item->packedSize == 0)
                return QVariant();
            {
                int pct = static_cast<int>((1.0 - static_cast<double>(item->packedSize) / item->size) * 100.0);
                pct = std::max(0, std::min(99, pct));
                return QString("%1%").arg(pct);
            }
        case ColMethod:
            return (item->hasEntry && !item->compressionMethod.isEmpty())
                ? QVariant(item->compressionMethod) : QVariant();
        case ColType:     return item->isDir ? tr("Folder") : (item->hasEntry ? tr("File") : tr("Folder"));
        case ColModified:
            if (item->modified > 86400 * 365)
                return QDateTime::fromSecsSinceEpoch(item->modified).toString("yyyy-MM-dd HH:mm");
            return item->hasEntry ? "-" : "";
        case ColCRC:
            return item->hasEntry && item->crc
                ? QString::number(item->crc, 16).toUpper().rightJustified(8, '0')
                : "-";
        case ColPermissions:
            if (!item->hasEntry || item->permissions == 0) return "-";
        {
            QString s;
            uint32_t m = item->permissions;
            s += (m & 00400) ? 'r' : '-';
            s += (m & 00200) ? 'w' : '-';
            s += (m & 00100) ? 'x' : '-';
            s += (m & 00040) ? 'r' : '-';
            s += (m & 00020) ? 'w' : '-';
            s += (m & 00010) ? 'x' : '-';
            s += (m & 00004) ? 'r' : '-';
            s += (m & 00002) ? 'w' : '-';
            s += (m & 00001) ? 'x' : '-';
            return s;
        }
        case ColComment:
            return item->comment;
        }
    }

    if (role == Qt::DecorationRole && index.column() == ColName)
        return iconForFile(item->name, item->isDir);

    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColName:     return tr("Name");
    case ColSize:     return tr("Size");
    case ColPacked:   return tr("Packed");
    case ColRatio:    return tr("Ratio");
    case ColMethod:   return tr("Method");
    case ColType:     return tr("Type");
    case ColModified: return tr("Modified");
    case ColCRC:      return tr("CRC");
    case ColPermissions: return tr("Permissions");
    case ColComment:     return tr("Comment");
    }
    return {};
}

Qt::ItemFlags FileListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QModelIndex FileListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) return {};
    Item* parentItem = parent.isValid()
        ? static_cast<Item*>(parent.internalPointer())
        : m_rootItem;
    if (row < 0 || row >= (int)parentItem->children.size()) return {};
    return createIndex(row, column, parentItem->children[row]);
}

QModelIndex FileListModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) return {};
    auto* item = static_cast<Item*>(index.internalPointer());
    if (!item || !item->parentItem || item->parentItem == m_rootItem) return {};
    auto* grandpa = item->parentItem->parentItem;
    if (!grandpa) return {};
    auto it = std::find(grandpa->children.begin(), grandpa->children.end(), item->parentItem);
    if (it == grandpa->children.end()) return {};
    int row = (int)(it - grandpa->children.begin());
    return createIndex(row, 0, item->parentItem);
}

// ── Sorting ────────────────────────────────────────────────────────────
void FileListModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    rebuild();
}

void FileListModel::sortItems()
{
    std::sort(m_rootItem->children.begin(), m_rootItem->children.end(),
        [this](const Item* a, const Item* b) {
            // ".." always first
            if (a->isParent) return true;
            if (b->isParent) return false;

            // Directories before files
            if (a->isDir != b->isDir) return a->isDir > b->isDir;

            auto cmp = [&]() -> int {
                switch (m_sortColumn)
                {
                case ColName:
                    return a->name.compare(b->name, Qt::CaseInsensitive);
                case ColSize:
                    if (a->size < b->size) return -1;
                    if (a->size > b->size) return 1;
                    return 0;
                case ColPacked:
                    if (a->packedSize < b->packedSize) return -1;
                    if (a->packedSize > b->packedSize) return 1;
                    return 0;
                case ColRatio:
                {
                    double ra = (a->size > 0 && a->packedSize > 0)
                        ? (1.0 - static_cast<double>(a->packedSize) / a->size) : 0.0;
                    double rb = (b->size > 0 && b->packedSize > 0)
                        ? (1.0 - static_cast<double>(b->packedSize) / b->size) : 0.0;
                    if (ra < rb) return -1;
                    if (ra > rb) return 1;
                    return 0;
                }
                case ColMethod:
                    return a->compressionMethod.compare(b->compressionMethod, Qt::CaseInsensitive);
                case ColType:
                    return (a->isDir ? QStringLiteral("Folder") :
                            a->hasEntry ? QStringLiteral("File") : QString())
                        .compare(b->isDir ? QStringLiteral("Folder") :
                                 b->hasEntry ? QStringLiteral("File") : QString(),
                                 Qt::CaseInsensitive);
                case ColModified:
                    if (a->modified < b->modified) return -1;
                    if (a->modified > b->modified) return 1;
                    return 0;
                case ColCRC:
                    if (a->crc < b->crc) return -1;
                    if (a->crc > b->crc) return 1;
                    return 0;
                case ColPermissions:
                    if (a->permissions < b->permissions) return -1;
                    if (a->permissions > b->permissions) return 1;
                    return 0;
                }
                return 0;
            };
            int r = cmp();
            return m_sortOrder == Qt::AscendingOrder ? r < 0 : r > 0;
        });
}

// ── Helpers ────────────────────────────────────────────────────────────
QString FileListModel::relativePath(const QString& full) const
{
    if (full.startsWith(m_currentDirPrefix))
        return full.mid(m_currentDirPrefix.size());
    return full;
}

QIcon FileListModel::iconForFile(const QString& name, bool isDir) const
{
    if (!m_iconProvider)
        m_iconProvider = new QFileIconProvider();

    if (isDir || name == "..")
        return m_iconProvider->icon(QFileIconProvider::Folder);

    QFileInfo fi(name);
    return m_iconProvider->icon(fi);
}
