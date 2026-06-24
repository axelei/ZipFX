#include <QTest>
#include <QObject>

#include "engine/FileSignature.h"
#include "TestUtils.h"

#include <fstream>

class tst_FileSignature : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Create synthetic magic-byte files
        auto write = [](const std::string& name, const std::vector<uint8_t>& bytes) {
            auto path = baseTempDir() / name;
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            out.close();
            return path;
        };

        m_zipFile = write("test.zip", {0x50, 0x4B, 0x03, 0x04, 0x00, 0x00});
        m_7zFile  = write("test.7z",  {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C});
        m_rarFile = write("test.rar", {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00});
        m_rar5File= write("test.rar5",{'R', 'a', 'r', '!', 0x1A, 0x07, 0x01});
        m_gzFile  = write("test.gz",  {0x1F, 0x8B});
        m_cabFile = write("test.cab", {'M', 'S', 'C', 'F'});
        m_xarFile = write("test.xar", {'x', 'a', 'r', '!'});
        m_empty   = write("empty.bin", {});

        // Disk image magics
        m_vhdFile  = write("test.vhd",  {'c', 'o', 'n', 'e', 'c', 't', 'i', 'x'});
        m_vmdkFile = write("test.vmdk", {'K', 'D', 'M', 'V'});
        m_qcowFile = write("test.qcow", {'Q', 'F', 'I', 0xFB});
        m_nrgFile  = write("test.nrg",  {'N', 'E', 'R', '5'});
        m_nrg5File = write("test.nrg5", {'N', 'e', 'r', 'o', '5'});
        m_adfFile  = write("test.adf",  {'D', 'O', 'S'});

        // Game archive magics
        m_wadIWAD  = write("iwad.wad", {'I', 'W', 'A', 'D'});
        m_wadPWAD  = write("pwad.wad", {'P', 'W', 'A', 'D'});
        m_wadWAD2  = write("wad2.wad", {'W', 'A', 'D', '2'});
        m_wadWAD3  = write("wad3.wad", {'W', 'A', 'D', '3'});
        m_pakFile  = write("test.pak", {'P', 'A', 'C', 'K'});
        m_grpFile  = write("test.grp", {'K', 'e', 'n', 'S', 'i', 'l', 'v', 'e', 'r', 'm', 'a', 'n'});
        m_hogFile  = write("test.hog", {'H', 'O', 'G', 0xF0});
        m_vpkFile  = write("test.vpk", {0x34, 0x12, 0xAA, 0x55});
        m_mpqFile  = write("test.mpq", {'M', 'P', 'Q', 0x1A});

        // Additional formats
        m_cpioFile = write("test.cpio", {'0', '7', '0', '7', '0', '1'});
        m_lhaFile  = write("test.lha", {0, 0, '-', 'l', 0, 0, '-'});
        m_arFile   = write("test.a",   {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'});
    }

    void testZip()  { QCOMPARE(FileSignature::Detect(m_zipFile.string()),  ArchiveType::Zip); }
    void test7z()   { QCOMPARE(FileSignature::Detect(m_7zFile.string()),   ArchiveType::SevenZip); }
    void testRar()  { QCOMPARE(FileSignature::Detect(m_rarFile.string()),  ArchiveType::Rar); }
    void testRar5() { QCOMPARE(FileSignature::Detect(m_rar5File.string()), ArchiveType::Rar5); }
    void testGzip() { QCOMPARE(FileSignature::Detect(m_gzFile.string()),   ArchiveType::Gzip); }
    void testCab()  { QCOMPARE(FileSignature::Detect(m_cabFile.string()),  ArchiveType::Cab); }
    void testXar()  { QCOMPARE(FileSignature::Detect(m_xarFile.string()),  ArchiveType::Xar); }

    void testCpio()
    {
        QCOMPARE(FileSignature::Detect(m_cpioFile.string()), ArchiveType::Cpio);
    }
    void testLha()
    {
        QCOMPARE(FileSignature::Detect(m_lhaFile.string()), ArchiveType::Lha);
    }
    void testAr()
    {
        QCOMPARE(FileSignature::Detect(m_arFile.string()), ArchiveType::Ar);
    }

    void testVhd()
    {
        QCOMPARE(FileSignature::Detect(m_vhdFile.string()), ArchiveType::Vhd);
    }
    void testVmdk()
    {
        QCOMPARE(FileSignature::Detect(m_vmdkFile.string()), ArchiveType::Vmdk);
    }
    void testQcow()
    {
        QCOMPARE(FileSignature::Detect(m_qcowFile.string()), ArchiveType::Qcow);
    }
    void testNrg()
    {
        QCOMPARE(FileSignature::Detect(m_nrgFile.string()), ArchiveType::Nrg);
    }
    void testNrg5()
    {
        QCOMPARE(FileSignature::Detect(m_nrg5File.string()), ArchiveType::Nrg);
    }
    void testAdf()
    {
        QCOMPARE(FileSignature::Detect(m_adfFile.string()), ArchiveType::Adf);
    }

    // ── Game archive magics ──
    void testWadIWAD()
    {
        QCOMPARE(FileSignature::Detect(m_wadIWAD.string()), ArchiveType::Wad);
    }
    void testWadPWAD()
    {
        QCOMPARE(FileSignature::Detect(m_wadPWAD.string()), ArchiveType::Wad);
    }
    void testWadWAD2()
    {
        QCOMPARE(FileSignature::Detect(m_wadWAD2.string()), ArchiveType::Wad);
    }
    void testWadWAD3()
    {
        QCOMPARE(FileSignature::Detect(m_wadWAD3.string()), ArchiveType::Wad);
    }
    void testPakMagic()
    {
        QCOMPARE(FileSignature::Detect(m_pakFile.string()), ArchiveType::Pak);
    }
    void testGrpMagic()
    {
        QCOMPARE(FileSignature::Detect(m_grpFile.string()), ArchiveType::Grp);
    }
    void testHogMagic()
    {
        QCOMPARE(FileSignature::Detect(m_hogFile.string()), ArchiveType::Hog);
    }
    void testVpkMagic()
    {
        QCOMPARE(FileSignature::Detect(m_vpkFile.string()), ArchiveType::Vpk);
    }
    void testMpqMagic()
    {
        QCOMPARE(FileSignature::Detect(m_mpqFile.string()), ArchiveType::Mpq);
    }

    void testEmpty()
    {
        QCOMPARE(FileSignature::Detect(m_empty.string()), ArchiveType::Unknown);
    }

    void testNonExistent()
    {
        QCOMPARE(FileSignature::Detect("/nonexistent/file.bin"), ArchiveType::Unknown);
    }

private:
    fs::path m_zipFile, m_7zFile, m_rarFile, m_rar5File;
    fs::path m_gzFile, m_cabFile, m_xarFile, m_empty;
    fs::path m_cpioFile, m_lhaFile, m_arFile;
    fs::path m_vhdFile, m_vmdkFile, m_qcowFile, m_nrgFile, m_nrg5File, m_adfFile;
    fs::path m_wadIWAD, m_wadPWAD, m_wadWAD2, m_wadWAD3;
    fs::path m_pakFile, m_grpFile, m_hogFile, m_vpkFile, m_mpqFile;
};

QTEST_MAIN(tst_FileSignature)
#include "tst_FileSignature.moc"
