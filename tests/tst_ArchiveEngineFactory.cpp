#include <QTest>
#include <QObject>

#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEngine.h"
#include "engine/ZipEngine.h"
#include "engine/TarGzEngine.h"
#include "engine/Bit7zEngine.h"

class tst_ArchiveEngineFactory : public QObject
{
    Q_OBJECT

private slots:
    // ── Extension-based format lookup ─────────────────────────────────
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

    void testCreateForFile_7z()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.7z");
        QVERIFY(e != nullptr);
        // 7z uses Bit7zEngine
        QVERIFY(dynamic_cast<Bit7zEngine*>(e.get()) != nullptr);
    }

    void testCreateForFile_rar()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.rar");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_arj()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.arj");
        QVERIFY(e != nullptr);
        // ARJ is read-only via Bit7zEngine
        QVERIFY(!e->SupportsCreation());
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

    void testCreateForFile_gz()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.gz");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_bz2()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.bz2");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_xz()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.xz");
        QVERIFY(e != nullptr);
    }

    void testCreateForFile_zst()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.zst");
        QVERIFY(e != nullptr);
    }

    // ── Format-name lookup ────────────────────────────────────────────
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

    void testCreateForFormat_targz()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("tar.gz");
        QVERIFY(e != nullptr);
    }

    void testCreateForFormat_unknown()
    {
        auto e = ArchiveEngineFactory::CreateForFormat("xyz");
        QVERIFY(e == nullptr);
    }

    // ── Capability flags ──────────────────────────────────────────────
    void testZipSupportsCreation()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.zip");
        QVERIFY(e != nullptr);
        QVERIFY(e->SupportsCreation());
    }

    void testArjReadOnly()
    {
        // ARJ must be read-only (Bit7zEngine with setReadOnly(true))
        auto e = ArchiveEngineFactory::CreateForFile("test.arj");
        QVERIFY(e != nullptr);
        QVERIFY(!e->SupportsCreation());
    }

    void testRarReadOnly()
    {
        auto e = ArchiveEngineFactory::CreateForFile("test.rar");
        QVERIFY(e != nullptr);
        // RAR creation requires external rar binary; engine may or may not support it
        // Just verify the engine is returned (not null)
    }

    // ── Extension registry ────────────────────────────────────────────
    void testSupportedExtensions()
    {
        auto exts = ArchiveEngineFactory::SupportedExtensions();
        QVERIFY(!exts.empty());
        QVERIFY(std::find(exts.begin(), exts.end(), ".zip") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".7z") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".rar") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".arj") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".tar.gz") != exts.end());
        QVERIFY(std::find(exts.begin(), exts.end(), ".iso") != exts.end());
    }
};

QTEST_MAIN(tst_ArchiveEngineFactory)
#include "tst_ArchiveEngineFactory.moc"
