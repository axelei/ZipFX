#include <QTest>
#include <QObject>

#include "engine/ZipEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

class tst_ZipEngine : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;
    fs::path m_archive;
    std::string m_contentA = "Hello ZIP world!";
    std::string m_contentB = "File B content here";

private slots:
    void initTestCase()
    {
        m_archive = m_dir.path / "test.zip";
        createTempFile("src/a.txt", m_contentA);
        createTempFile("src/sub/b.txt", m_contentB);
    }

    void testCreateAndAdd()
    {
        ZipEngine eng;
        QVERIFY(eng.Create(m_archive.string()));
        QVERIFY(eng.AddFile(
            (baseTempDir() / "src/a.txt").string(),
            "a.txt"));
        QVERIFY(eng.AddFile(
            (baseTempDir() / "src/sub/b.txt").string(),
            "sub/b.txt"));
        QVERIFY(eng.Save());
    }

    void testOpenAndList()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));
        QVERIFY(hasEntry(entries, "a.txt"));
        QVERIFY(hasEntry(entries, "sub/b.txt"));
    }

    void testReadFile()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto data = eng.ReadFile("a.txt");
        std::string result(data.begin(), data.end());
        QCOMPARE(result, m_contentA);

        data = eng.ReadFile("sub/b.txt");
        result.assign(data.begin(), data.end());
        QCOMPARE(result, m_contentB);
    }

    void testExtract()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto out = m_dir.path / "out_a.txt";
        QVERIFY(eng.Extract("a.txt", out.string()));
        QVERIFY(fs::exists(out));
        QCOMPARE(readFileContents(out), m_contentA);
    }

    void testExtractAll()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto outDir = m_dir.path / "extracted";
        QVERIFY(eng.ExtractAll(outDir.string()));
        QVERIFY(fs::exists(outDir / "a.txt"));
        QVERIFY(fs::exists(outDir / "sub/b.txt"));
        QCOMPARE(readFileContents(outDir / "a.txt"), m_contentA);
    }

    void testRemoveEntry()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));

        // Add a third file first to have something to remove
        createTempFile("src/to_remove.txt", "will be deleted");
        QVERIFY(eng.AddFile(
            (baseTempDir() / "src/to_remove.txt").string(),
            "to_remove.txt"));
        QVERIFY(eng.Save());

        // Re-open and remove
        QVERIFY(eng.Open(m_archive.string()));
        QVERIFY(eng.RemoveEntry("to_remove.txt"));
        QVERIFY(eng.Save());

        // Re-open and verify gone
        QVERIFY(eng.Open(m_archive.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));
        QVERIFY(!hasEntry(entries, "to_remove.txt"));
    }

    void testRenameEntry()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        QVERIFY(eng.RenameEntry("a.txt", "renamed.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(m_archive.string()));
        auto entries = eng.ListContents();
        QVERIFY(hasEntry(entries, "renamed.txt"));
        QVERIFY(!hasEntry(entries, "a.txt"));

        // Verify content preserved
        auto data = eng.ReadFile("renamed.txt");
        std::string result(data.begin(), data.end());
        QCOMPARE(result, m_contentA);
    }

    void testTestIntegrity()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        QVERIFY(eng.TestIntegrity());
    }

    void testOpenNonExistent()
    {
        ZipEngine eng;
        QVERIFY(!eng.Open((m_dir.path / "nope.zip").string()));
    }

    void testReadNonExistent()
    {
        ZipEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto data = eng.ReadFile("nonexistent.txt");
        QVERIFY(data.empty());
    }

    void testListEmpty()
    {
        auto emptyPath = m_dir.path / "empty.zip";
        {
            ZipEngine eng;
            QVERIFY(eng.Create(emptyPath.string()));
            QVERIFY(eng.Save());
        }
        ZipEngine eng;
        QVERIFY(eng.Open(emptyPath.string()));
        QVERIFY(eng.ListContents().empty());
    }
};

QTEST_MAIN(tst_ZipEngine)
#include "tst_ZipEngine.moc"
