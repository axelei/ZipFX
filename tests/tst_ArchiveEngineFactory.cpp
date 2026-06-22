#include <QTest>
#include <QObject>

#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEngine.h"
#include "engine/ZipEngine.h"
#include "engine/TarGzEngine.h"

class tst_ArchiveEngineFactory : public QObject
{
    Q_OBJECT

private slots:
    void testCreateForFile_zip()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.zip");
        QVERIFY(e != nullptr);
        QCOMPARE(e->FormatName(), "ZIP");
    }

    void testCreateForFile_tar()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.tar");
        QVERIFY(e != nullptr);
        QCOMPARE(e->FormatName(), "TAR.GZ");
    }

    void testCreateForFile_tgz()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.tgz");
        QVERIFY(e != nullptr);
        QCOMPARE(e->FormatName(), "TAR.GZ");
    }

    void testCreateForFile_tarGz()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.tar.gz");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_rar()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.rar");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_iso()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.iso");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_cab()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.cab");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_lzh()
    {
        // lzh triggers magic detection first; if no magic, falls to extension
        auto e = ArchiveEngineFactory::CreateForFile("test.lzh");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_xar()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.xar");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_cpio()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.cpio");
        QVERIFY(e != nullptr);
    }

    void testCreateForFormat_zip()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("zip");
        QVERIFY(e != nullptr);
        QVERIFY(e->SupportsCreation());
    }

    void testCreateForFormat_7z()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("7z");
        QVERIFY(e != nullptr);
    }

    void testCreateForFormat_rar()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("rar");
        QVERIFY(e != nullptr);
    }

    void testCreateForFormat_unknown()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("xyz");
        QVERIFY(e == nullptr);
    }

    void testSupportedExtensions()
    {
        auto exts = ArchiveEngineFactory::SupportedExtensions();
        QVERIFY(!exts.empty());
        QVERIFY(std::find(exts.begin(), exts.end(), ".zip") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".7z") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".rar") != exts.end());
    }
};

QTEST_MAIN(tst_ArchiveEngineFactory)
#include "tst_ArchiveEngineFactory.moc"
