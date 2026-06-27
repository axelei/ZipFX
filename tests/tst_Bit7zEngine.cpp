#include <QTest>
#include <QObject>

#include "engine/Bit7zEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

class tst_Bit7zEngine : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;
    bool m_libAvailable = false;

#define SKIP_IF_NO_LIB() \
    do { if (!m_libAvailable) QSKIP("7z.dll / 7-zip library not available"); } while (0)

private slots:
    void initTestCase()
    {
        Bit7zEngine probe;
        m_libAvailable = probe.isLibraryLoaded();

        createTempFile("b7src/hello.txt", "Hello from Bit7zEngine!");
        createTempFile("b7src/sub/world.txt", "World file");
    }

    // ── Library availability ──────────────────────────────────────────
    void testLibraryLoaded()
    {
        Bit7zEngine eng;
        // Just verify the call doesn't crash; result depends on runtime environment
        // We record it in m_libAvailable for all other tests
        (void)eng.isLibraryLoaded();
    }

    // ── Basic CRUD ────────────────────────────────────────────────────
    void testCreateAddSave()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        Bit7zEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/sub/world.txt").string(), "sub/world.txt"));
        QVERIFY(eng.Save());
        QVERIFY(fs::exists(archive));
    }

    void testOpenAndList()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        const auto& entries = eng.ListContents();
        QVERIFY(entries.size() >= 2);
        QVERIFY(hasEntry(entries, "hello.txt"));
        QVERIFY(hasEntry(entries, "sub/world.txt"));
    }

    void testReadFile()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testExtract()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        auto out = m_dir.path / "extract_out.txt";
        QVERIFY(eng.Extract("hello.txt", out.string()));
        QVERIFY(fs::exists(out));
        QCOMPARE(readFileContents(out), std::string("Hello from Bit7zEngine!"));
    }

    void testExtractAll()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        auto outDir = m_dir.path / "extractall_out";
        QVERIFY(eng.ExtractAll(outDir.string()));
        QVERIFY(fs::exists(outDir / "hello.txt"));
        QVERIFY(fs::exists(outDir / "sub/world.txt"));
    }

    void testRemoveEntry()
    {
        SKIP_IF_NO_LIB();

        // Create an archive with two entries, then delete one
        auto archive = m_dir.path / "remove.7z";
        createTempFile("b7src/keep.txt", "keep this");
        createTempFile("b7src/delete.txt", "delete this");

        Bit7zEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/keep.txt").string(), "keep.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/delete.txt").string(), "delete.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        QVERIFY(eng.RemoveEntry("delete.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        const auto& entries = eng.ListContents();
        QVERIFY(hasEntry(entries, "keep.txt"));
        QVERIFY(!hasEntry(entries, "delete.txt"));
    }

    void testTestIntegrity()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        QVERIFY(eng.TestIntegrity());
    }

    // ── Compression options ───────────────────────────────────────────
    void testCompressionLevelStore()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "level0.7z";
        Bit7zEngine eng;
        eng.setCompressionLevel(0); // store
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        // Verify we can read back
        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testCompressionMethodLzma()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "method_lzma.7z";
        Bit7zEngine eng;
        eng.setCompressionMethod(4); // LZMA
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testCompressionMethodLzma2()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "method_lzma2.7z";
        Bit7zEngine eng;
        eng.setCompressionMethod(5); // LZMA2
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testCompressionMethodBzip2()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "method_bzip2.7z";
        Bit7zEngine eng;
        eng.setCompressionMethod(3); // BZip2
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testSolidModeOff()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "nonsolid.7z";
        Bit7zEngine eng;
        eng.setSolidMode(false);
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/sub/world.txt").string(), "world.txt"));
        QVERIFY(eng.Save());

        // Non-solid: SupportsViewFile() should return true
        QVERIFY(eng.Open(archive.string()));
        QVERIFY(eng.SupportsViewFile());
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testSolidModeOn()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "solid.7z";
        Bit7zEngine eng;
        eng.setSolidMode(true);
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/sub/world.txt").string(), "world.txt"));
        QVERIFY(eng.Save());

        // Solid: SupportsViewFile() returns false (can't random-access solid stream)
        QVERIFY(eng.Open(archive.string()));
        QVERIFY(!eng.SupportsViewFile());
    }

    void testDictionarySize()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "dict.7z";
        Bit7zEngine eng;
        eng.setDictionarySize(1 * 1024 * 1024); // 1 MB dictionary
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    void testThreadsCount()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "threads.7z";
        Bit7zEngine eng;
        eng.setThreadsCount(2);
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "b7src/hello.txt").string(), "hello.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("hello.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("Hello from Bit7zEngine!"));
    }

    // ── Read-only mode ────────────────────────────────────────────────
    void testReadOnlyPreventsCreation()
    {
        Bit7zEngine eng;
        eng.setReadOnly(true);
        QVERIFY(!eng.SupportsCreation());
    }

    // ── Error cases ───────────────────────────────────────────────────
    void testOpenNonExistent()
    {
        Bit7zEngine eng;
        QVERIFY(!eng.Open((m_dir.path / "nope.7z").string()));
    }

    void testReadNonExistentEntry()
    {
        SKIP_IF_NO_LIB();

        auto archive = m_dir.path / "basic.7z";
        if (!fs::exists(archive)) QSKIP("archive not created");

        Bit7zEngine eng;
        QVERIFY(eng.Open(archive.string()));
        auto data = eng.ReadFile("does_not_exist.txt");
        QVERIFY(data.empty());
    }
};

QTEST_MAIN(tst_Bit7zEngine)
#include "tst_Bit7zEngine.moc"
