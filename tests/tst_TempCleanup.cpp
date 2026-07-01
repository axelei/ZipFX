#include <QTest>
#include <QObject>
#include <fstream>

#include "dnd/TempCleanup.h"
#include "TestUtils.h"

namespace fs = std::filesystem;

class tst_TempCleanup : public QObject
{
    Q_OBJECT

private:
    TempDir m_dir;

private slots:
    void initTestCase()
    {
        // m_dir is set up by TempDir constructor
    }

    void cleanup()
    {
        // Reset the global state between tests by cleaning up anything
        // that was registered and not cleaned up yet.
        TempCleanup::cleanupAll();
    }

    void testRegisterAndCleanSinglePath()
    {
        fs::path testDir = m_dir.path / "single";
        fs::create_directories(testDir);

        // Create a file inside
        {
            std::ofstream(testDir / "file.txt") << "hello";
        }

        QVERIFY(fs::exists(testDir));
        TempCleanup::registerPath(testDir);
        TempCleanup::cleanupAll();

        QVERIFY(!fs::exists(testDir));
    }

    void testRegisterAndCleanMultiplePaths()
    {
        fs::path dir1 = m_dir.path / "multi1";
        fs::path dir2 = m_dir.path / "multi2";
        fs::create_directories(dir1);
        fs::create_directories(dir2);

        {
            std::ofstream(dir1 / "a.txt") << "a";
            std::ofstream(dir2 / "b.txt") << "b";
        }

        TempCleanup::registerPath(dir1);
        TempCleanup::registerPath(dir2);

        QVERIFY(fs::exists(dir1));
        QVERIFY(fs::exists(dir2));

        TempCleanup::cleanupAll();

        QVERIFY(!fs::exists(dir1));
        QVERIFY(!fs::exists(dir2));
    }

    void testUnregisterPath()
    {
        fs::path dir1 = m_dir.path / "unreg1";
        fs::path dir2 = m_dir.path / "unreg2";
        fs::create_directories(dir1);
        fs::create_directories(dir2);

        TempCleanup::registerPath(dir1);
        TempCleanup::registerPath(dir2);

        // Remove dir1 from the cleanup list
        TempCleanup::unregisterPath(dir1);

        TempCleanup::cleanupAll();

        // dir1 should still exist (unregistered)
        QVERIFY(fs::exists(dir1));

        // dir2 should be gone
        QVERIFY(!fs::exists(dir2));

        // Clean up dir1 manually
        fs::remove_all(dir1);
    }

    void testCleanupNonexistentPath()
    {
        // Should not crash when trying to clean a path that was deleted early
        fs::path testDir = m_dir.path / "already_gone";
        fs::create_directories(testDir);
        TempCleanup::registerPath(testDir);

        // Delete it before cleanup
        fs::remove_all(testDir);

        // Should not throw or crash
        TempCleanup::cleanupAll();
        QVERIFY(true); // reached here = no crash
    }

    void testCleanupIsIdempotent()
    {
        // Calling cleanupAll twice should be safe
        fs::path testDir = m_dir.path / "idempotent";
        fs::create_directories(testDir);
        TempCleanup::registerPath(testDir);

        TempCleanup::cleanupAll();
        TempCleanup::cleanupAll(); // second call

        QVERIFY(!fs::exists(testDir));
    }

    void testRegisterSamePathTwice()
    {
        fs::path testDir = m_dir.path / "dup";
        fs::create_directories(testDir);

        TempCleanup::registerPath(testDir);
        TempCleanup::registerPath(testDir);

        TempCleanup::cleanupAll();

        // Should have been cleaned up without error
        QVERIFY(!fs::exists(testDir));
    }

    void testCleanupEmptyList()
    {
        // cleanupAll on an empty list should not crash
        TempCleanup::cleanupAll();
        QVERIFY(true);
    }

    void testDeepDirectoryCleanup()
    {
        fs::path deepDir = m_dir.path / "deep" / "nested" / "structure";
        fs::create_directories(deepDir);

        {
            std::ofstream(deepDir / "file1.txt") << "data1";
            std::ofstream(deepDir / "file2.txt") << "data2";
        }

        fs::path rootDir = m_dir.path / "deep";
        TempCleanup::registerPath(rootDir);
        TempCleanup::cleanupAll();

        QVERIFY(!fs::exists(rootDir));
    }
};

QTEST_MAIN(tst_TempCleanup)
#include "tst_TempCleanup.moc"
