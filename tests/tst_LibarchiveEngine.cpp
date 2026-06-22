#include <QTest>
#include <QObject>

#include "engine/LibarchiveEngine.h"
#include "engine/Bit7zEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

#include <archive.h>

class tst_LibarchiveEngine : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;
    fs::path m_7zFile;

private slots:
    void initTestCase()
    {
        m_7zFile = m_dir.path / "test.7z";

        // Create a test 7z using Bit7zEngine (if 7z.dll available)
        auto eng = std::make_unique<Bit7zEngine>();
        if (!eng->isLibraryLoaded())
            return;

        // Create with a single file
        auto srcFile = createTempFile("7ztest.txt", "Hello from libarchive test");
        QVERIFY(eng->Create(m_7zFile.string()));
        QVERIFY(eng->AddFile(srcFile.string(), "7ztest.txt"));
        QVERIFY(eng->Save());
    }

    void testOpen7z()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z", false, "LZMA2");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
    }

    void testList()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(1));
        QCOMPARE(entries[0].name, "7ztest.txt");
    }

    void testReadFile()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
        auto data = eng.ReadFile("7ztest.txt");
        std::string result(data.begin(), data.end());
        QCOMPARE(result, "Hello from libarchive test");
    }

    void testExtract()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
        auto out = m_dir.path / "out.txt";
        QVERIFY(eng.Extract("7ztest.txt", out.string()));
        QVERIFY(fs::exists(out));
        QCOMPARE(readFileContents(out), "Hello from libarchive test");
    }

    void testExtractAll()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
        auto outDir = m_dir.path / "extracted";
        QVERIFY(eng.ExtractAll(outDir.string()));
        QVERIFY(fs::exists(outDir / "7ztest.txt"));
    }

    void testTestIntegrity()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zFile))
            QSKIP("7z test archive not available");
        QVERIFY(eng.Open(m_7zFile.string()));
        QVERIFY(eng.TestIntegrity());
    }

    void testOpenNonExistent()
    {
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        QVERIFY(!eng.Open((m_dir.path / "nope.7z").string()));
    }

    void testInvalidData()
    {
        auto badFile = m_dir.path / "bad.dat";
        createTempFile("bad.dat", "not an archive");
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        QVERIFY(!eng.Open(badFile.string()));
    }
};

QTEST_MAIN(tst_LibarchiveEngine)
#include "tst_LibarchiveEngine.moc"
