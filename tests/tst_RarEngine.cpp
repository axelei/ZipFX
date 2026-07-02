#include <QTest>
#include <QObject>

#include "engine/RarEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

class tst_RarEngine : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;

private slots:
    void initTestCase()
    {
        createTempFile("rarsrc/a.txt", "Hello RAR world!");
        createTempFile("rarsrc/b.txt", "Second file content");
    }

    // Regression test: adding a file to an existing RAR archive and saving
    // used to fail on Windows with rar.exe exit code 6 ("cannot open
    // file"), because RarEngine::Save() invoked rar.exe while m_reader
    // (Bit7zEngine or LibarchiveEngine, opened by Open()) still held the
    // archive file open for reading — Windows won't let rar.exe open the
    // same file for writing while that handle is live.
    void testAddFileToExistingArchive()
    {
        if (!RarEngine::isAvailable())
            QSKIP("rar.exe / WinRAR not installed");

        auto archive = m_dir.path / "existing.rar";

        // Create the initial archive with one file.
        {
            RarEngine eng;
            QVERIFY(eng.Create(archive.string()));
            QVERIFY(eng.AddFile((baseTempDir() / "rarsrc/a.txt").string(), "a.txt"));
            QVERIFY(eng.Save());
        }

        // Open it (as the UI would), then add a second file and save —
        // this is the exact drag-and-drop-into-an-open-archive scenario.
        RarEngine eng;
        QVERIFY(eng.Open(archive.string()));
        QCOMPARE(eng.ListContents().size(), size_t(1));

        QVERIFY(eng.AddFile((baseTempDir() / "rarsrc/b.txt").string(), "b.txt"));
        QVERIFY(eng.Save());

        // Re-open independently to verify the archive on disk actually has
        // both entries now (not just that Save() claimed success).
        RarEngine verify;
        QVERIFY(verify.Open(archive.string()));
        QCOMPARE(verify.ListContents().size(), size_t(2));

        auto data = verify.ReadFile("b.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Second file content"));
    }

    void testRemoveEntryFromExistingArchive()
    {
        if (!RarEngine::isAvailable())
            QSKIP("rar.exe / WinRAR not installed");

        auto archive = m_dir.path / "remove_test.rar";

        {
            RarEngine eng;
            QVERIFY(eng.Create(archive.string()));
            QVERIFY(eng.AddFile((baseTempDir() / "rarsrc/a.txt").string(), "a.txt"));
            QVERIFY(eng.AddFile((baseTempDir() / "rarsrc/b.txt").string(), "b.txt"));
            QVERIFY(eng.Save());
        }

        RarEngine eng;
        QVERIFY(eng.Open(archive.string()));
        QCOMPARE(eng.ListContents().size(), size_t(2));
        QVERIFY(eng.RemoveEntry("a.txt"));

        RarEngine verify;
        QVERIFY(verify.Open(archive.string()));
        QCOMPARE(verify.ListContents().size(), size_t(1));
        QCOMPARE(verify.ListContents()[0].name, std::string("b.txt"));
    }
};

QTEST_MAIN(tst_RarEngine)
#include "tst_RarEngine.moc"
