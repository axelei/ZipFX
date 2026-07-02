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
    fs::path m_7zEncryptedFile;

private slots:
    void initTestCase()
    {
        m_7zFile = m_dir.path / "test.7z";
        m_7zEncryptedFile = m_dir.path / "test_encrypted.7z";

        // Create test 7z archives using Bit7zEngine (if 7z.dll available)
        auto eng = std::make_unique<Bit7zEngine>();
        if (!eng->isLibraryLoaded())
            return;

        // Create with a single file
        auto srcFile = createTempFile("7ztest.txt", "Hello from libarchive test");
        QVERIFY(eng->Create(m_7zFile.string()));
        QVERIFY(eng->AddFile(srcFile.string(), "7ztest.txt"));
        QVERIFY(eng->Save());

        // Same content, password-protected — used to verify LibarchiveEngine
        // (the read-only fallback used when 7z.dll/Bit7z isn't available)
        // actually applies the passphrase instead of just storing it.
        auto encEng = std::make_unique<Bit7zEngine>();
        encEng->setPassword("hunter2");
        QVERIFY(encEng->Create(m_7zEncryptedFile.string()));
        QVERIFY(encEng->AddFile(srcFile.string(), "7ztest.txt"));
        QVERIFY(encEng->Save());
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

    void testPasswordAppliedButUnsupportedByLibarchive()
    {
        // libarchive's 7z/RAR/RAR5 readers unconditionally reject encrypted
        // content ("is encrypted, but currently not supported" /
        // "RAR encryption support unavailable.") regardless of whether a
        // passphrase was supplied via archive_read_add_passphrase() — this
        // is a hard limitation of the library itself (verified by reading
        // its source), not something LibarchiveEngine's password wiring can
        // work around. This is only reachable in ZipFX when 7z.dll/Bit7z
        // isn't installed (RAR falls back here, and 7z falls back here);
        // when Bit7z *is* available it's used instead and does support
        // decryption. This test documents that the failure is a clean,
        // detectable one (empty result) rather than garbage/crash — not
        // that the password round-trips.
        LibarchiveEngine eng(
            {archive_read_support_format_7zip}, "7z");
        if (!fs::exists(m_7zEncryptedFile))
            QSKIP("encrypted 7z test archive not available");

        QVERIFY(eng.SupportsEncryption());
        QVERIFY(eng.Open(m_7zEncryptedFile.string()));

        const auto& entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(1));
        QVERIFY(entries[0].isEncrypted);
        // The whole point of this test: the UI can distinguish "this build
        // can't decrypt" from "wrong password" via this non-empty reason.
        QVERIFY(!eng.EncryptionUnavailableReason().empty());

        eng.setPassword("hunter2"); // the actual, correct password
        auto data = eng.ReadFile("7ztest.txt");
        QVERIFY(data.empty());
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
