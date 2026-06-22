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
    }

    void testZip()  { QCOMPARE(FileSignature::Detect(m_zipFile.string()),  ArchiveType::Zip); }
    void test7z()   { QCOMPARE(FileSignature::Detect(m_7zFile.string()),   ArchiveType::SevenZip); }
    void testRar()  { QCOMPARE(FileSignature::Detect(m_rarFile.string()),  ArchiveType::Rar); }
    void testRar5() { QCOMPARE(FileSignature::Detect(m_rar5File.string()), ArchiveType::Rar5); }
    void testGzip() { QCOMPARE(FileSignature::Detect(m_gzFile.string()),   ArchiveType::Gzip); }
    void testCab()  { QCOMPARE(FileSignature::Detect(m_cabFile.string()),  ArchiveType::Cab); }
    void testXar()  { QCOMPARE(FileSignature::Detect(m_xarFile.string()),  ArchiveType::Xar); }

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
};

QTEST_MAIN(tst_FileSignature)
#include "tst_FileSignature.moc"
