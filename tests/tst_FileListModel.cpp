#include <QTest>
#include <QObject>
#include <QSignalSpy>

#include "ui/FileListModel.h"
#include "engine/ArchiveEntry.h"

class tst_FileListModel : public QObject
{
    Q_OBJECT

private:
    std::vector<ArchiveEntry> makeTestEntries() const
    {
        return {
            makeEntry("readme.txt", 100, 90, false),
            makeEntry("src/main.cpp", 200, 150, false),
            makeEntry("src/util.h", 50, 40, false),
            makeEntry("docs/index.html", 300, 280, false),
            makeEntry("docs/help/page1.html", 400, 350, false),
        };
    }

    static ArchiveEntry makeEntry(const std::string& path,
                                  uint64_t size, uint64_t packed, bool isDir)
    {
        ArchiveEntry e;
        e.name = path;
        e.path = path;
        e.size = size;
        e.packedSize = packed;
        e.isDirectory = isDir;
        e.crc = 0xDEADBEEF;
        return e;
    }

private slots:
    void testInitialState()
    {
        FileListModel model;
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.isFlatMode(), false);
        QVERIFY(model.currentDir().isEmpty());
    }

    void testSetEntriesFlat()
    {
        FileListModel model;
        model.setFlatMode(true);
        model.setEntries(makeTestEntries());
        // Flat mode: all 5 entries at root
        QCOMPARE(model.rowCount(), 5);
    }

    void testSetEntriesHierarchical()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());
        // At root, we see: docs/, src/, readme.txt
        QCOMPARE(model.rowCount(), 3);
    }

    void testNavigateInto()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());
        QVERIFY(!model.currentDir().isEmpty() || model.rowCount() > 0);

        model.navigateInto("src");
        QCOMPARE(model.currentDir(), "src");
        QCOMPARE(model.rowCount(), 3); // .., main.cpp, util.h

        // navigateInto() takes a name relative to the current directory
        // (matching how MainWindow calls it with a listed child entry's
        // name), so navigating further has to go through a real child of
        // the current directory — "docs" is a top-level sibling of "src",
        // not one of its children, so reaching it requires navigating back
        // up first.
        model.navigateUp();
        QCOMPARE(model.currentDir(), "");

        model.navigateInto("docs");
        QCOMPARE(model.currentDir(), "docs");

        model.navigateInto("help");
        QCOMPARE(model.currentDir(), "docs/help");
    }

    void testNavigateUp()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());

        model.navigateInto("docs");
        QCOMPARE(model.currentDir(), "docs");

        model.navigateInto("help");
        QCOMPARE(model.currentDir(), "docs/help");

        model.navigateUp();
        QCOMPARE(model.currentDir(), "docs");

        model.navigateUp();
        QVERIFY(model.currentDir().isEmpty());
    }

    void testColumnData()
    {
        FileListModel model;
        model.setFlatMode(true);
        model.setEntries(makeTestEntries());

        auto idx = model.index(0, FileListModel::ColName);
        QVERIFY(idx.isValid());
        QVERIFY(!idx.data(Qt::DisplayRole).toString().isEmpty());

        auto sizeIdx = model.index(0, FileListModel::ColSize);
        QVERIFY(!sizeIdx.data(Qt::DisplayRole).toString().isEmpty());
    }

    void testFilter()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());

        model.setFilterString("src");
        // Should show src/ directory
        QVERIFY(model.rowCount() >= 1);

        model.setFilterString("nonexistent");
        QCOMPARE(model.rowCount(), 0);

        model.setFilterString(QString());
        QVERIFY(model.rowCount() > 0);
    }

    void testSort()
    {
        FileListModel model;
        model.setFlatMode(true);
        model.setEntries(makeTestEntries());

        // Default sort by name ascending
        model.sort(FileListModel::ColName, Qt::AscendingOrder);
        auto idx0 = model.index(0, FileListModel::ColName);
        auto idx1 = model.index(1, FileListModel::ColName);
        auto name0 = idx0.data().toString();
        auto name1 = idx1.data().toString();
        QVERIFY(name0.toLower() <= name1.toLower());
    }

    void testSelectedEntryPaths()
    {
        FileListModel model;
        model.setFlatMode(true);
        model.setEntries(makeTestEntries());

        auto idx = model.index(0, 0);
        QVERIFY(idx.isValid());

        auto paths = model.selectedEntryPaths({idx});
        QCOMPARE(paths.size(), size_t(1));
    }

    void testClear()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());
        QVERIFY(model.rowCount() > 0);

        model.clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void testDirectoryChangedSignal()
    {
        FileListModel model;
        model.setEntries(makeTestEntries());

        QSignalSpy spy(&model, &FileListModel::directoryChanged);
        model.navigateInto("src");
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(tst_FileListModel)
#include "tst_FileListModel.moc"
