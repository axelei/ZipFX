#include <QTest>
#include <QObject>

#include "engine/TarGzEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

class tst_TarGzEngine : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;
    fs::path m_archive;
    std::string m_content = "Hello TAR.GZ!";

private slots:
    void initTestCase()
    {
        m_archive = m_dir.path / "test.tar.gz";
        createTempFile("src/file.txt", m_content);
    }

    void testCreateAndSave()
    {
        TarGzEngine eng;
        QVERIFY(eng.Create(m_archive.string()));
        QVERIFY(eng.AddFile(
            (baseTempDir() / "src/file.txt").string(),
            "file.txt"));
        QVERIFY(eng.Save());
    }

    void testOpenAndList()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(1));
        QVERIFY(hasEntry(entries, "file.txt"));
    }

    void testReadFile()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto data = eng.ReadFile("file.txt");
        std::string result(data.begin(), data.end());
        QCOMPARE(result, m_content);
    }

    void testExtract()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto out = m_dir.path / "out.txt";
        QVERIFY(eng.Extract("file.txt", out.string()));
        QCOMPARE(readFileContents(out), m_content);
    }

    void testExtractAll()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto outDir = m_dir.path / "extracted";
        QVERIFY(eng.ExtractAll(outDir.string()));
        QVERIFY(fs::exists(outDir / "file.txt"));
        QCOMPARE(readFileContents(outDir / "file.txt"), m_content);
    }

    void testTestIntegrity()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        QVERIFY(eng.TestIntegrity());
    }

    void testOpenNonExistent()
    {
        TarGzEngine eng;
        QVERIFY(!eng.Open((m_dir.path / "nope.tar.gz").string()));
    }
};

QTEST_MAIN(tst_TarGzEngine)
#include "tst_TarGzEngine.moc"
