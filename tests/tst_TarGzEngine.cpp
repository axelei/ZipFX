#include <QTest>
#include <QObject>

#include "engine/TarGzEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

#include <cstring>
#include <fstream>
#include <zlib.h>

// ── Raw .tar.gz helpers ────────────────────────────────────────────────
// Minimal tar header matching the layout in TarGzEngine.cpp
struct TestTarHeader
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};
static_assert(sizeof(TestTarHeader) == 512, "tar header must be 512 bytes");

static void tarOctal(char* buf, size_t len, uint64_t val)
{
    buf[len - 1] = '\0';
    for (size_t i = len - 1; i > 0; --i) { buf[i - 1] = char('0' + (val & 7)); val >>= 3; }
}

static uint64_t tarChecksum(const TestTarHeader* h)
{
    uint64_t sum = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(h);
    for (size_t i = 0; i < 512; ++i)
        sum += (i >= 148 && i < 156) ? ' ' : p[i];
    return sum;
}

// Write a plain-file entry followed by data into a buffer
static void appendTarRegular(std::vector<char>& buf, const std::string& name,
                              const std::string& data, uint64_t mtime = 0)
{
    TestTarHeader hdr = {};
    size_t n = std::min(name.size(), size_t(99));
    std::memcpy(hdr.name, name.c_str(), n);
    tarOctal(hdr.mode, 8, 0644);
    tarOctal(hdr.size, 12, data.size());
    tarOctal(hdr.mtime, 12, mtime);
    hdr.typeflag = '0';
    std::memcpy(hdr.magic, "ustar", 5);
    std::memcpy(hdr.version, "00", 2);
    tarOctal(hdr.chksum, 8, tarChecksum(&hdr));
    buf.insert(buf.end(), reinterpret_cast<const char*>(&hdr),
               reinterpret_cast<const char*>(&hdr) + 512);
    buf.insert(buf.end(), data.begin(), data.end());
    size_t pad = (512 - data.size() % 512) % 512;
    buf.insert(buf.end(), pad, '\0');
}

// Write a symlink entry (typeflag='2', no data blocks)
static void appendTarSymlink(std::vector<char>& buf, const std::string& name,
                             const std::string& target, uint64_t mtime = 0)
{
    TestTarHeader hdr = {};
    size_t n = std::min(name.size(), size_t(99));
    std::memcpy(hdr.name, name.c_str(), n);
    size_t t = std::min(target.size(), size_t(99));
    std::memcpy(hdr.linkname, target.c_str(), t);
    tarOctal(hdr.mode, 8, 0777);
    tarOctal(hdr.size, 12, 0);
    tarOctal(hdr.mtime, 12, mtime);
    hdr.typeflag = '2';
    std::memcpy(hdr.magic, "ustar", 5);
    std::memcpy(hdr.version, "00", 2);
    tarOctal(hdr.chksum, 8, tarChecksum(&hdr));
    buf.insert(buf.end(), reinterpret_cast<const char*>(&hdr),
               reinterpret_cast<const char*>(&hdr) + 512);
}

// Write a hardlink entry (typeflag='1')
static void appendTarHardlink(std::vector<char>& buf, const std::string& name,
                               const std::string& target, uint64_t mtime = 0)
{
    TestTarHeader hdr = {};
    size_t n = std::min(name.size(), size_t(99));
    std::memcpy(hdr.name, name.c_str(), n);
    size_t t = std::min(target.size(), size_t(99));
    std::memcpy(hdr.linkname, target.c_str(), t);
    tarOctal(hdr.mode, 8, 0644);
    tarOctal(hdr.size, 12, 0);
    tarOctal(hdr.mtime, 12, mtime);
    hdr.typeflag = '1';
    std::memcpy(hdr.magic, "ustar", 5);
    std::memcpy(hdr.version, "00", 2);
    tarOctal(hdr.chksum, 8, tarChecksum(&hdr));
    buf.insert(buf.end(), reinterpret_cast<const char*>(&hdr),
               reinterpret_cast<const char*>(&hdr) + 512);
}

// Gzip-compress raw tar bytes and write to path
static bool writeTarGz(const fs::path& path, const std::vector<char>& tarData)
{
    // End-of-archive: two zero blocks
    std::vector<char> all = tarData;
    all.insert(all.end(), 1024, '\0');

    gzFile gz = gzopen(path.string().c_str(), "wb");
    if (!gz) return false;
    gzwrite(gz, all.data(), static_cast<unsigned>(all.size()));
    gzclose(gz);
    return true;
}

// ── Test class ────────────────────────────────────────────────────────
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

    void testReadFilePartial()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        size_t half = m_content.size() / 2;
        auto data = eng.ReadFilePartial("file.txt", half);
        QVERIFY(!data.empty());
        QVERIFY(data.size() <= half);
        QCOMPARE(std::string(data.begin(), data.end()),
                 m_content.substr(0, data.size()));
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

    void testMultipleFiles()
    {
        std::string c1 = "alpha content";
        std::string c2 = "beta content";
        createTempFile("src/alpha.txt", c1);
        createTempFile("src/beta.txt", c2);

        auto archive = m_dir.path / "multi.tar.gz";
        TarGzEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "src/alpha.txt").string(), "alpha.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "src/beta.txt").string(), "beta.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));
        QVERIFY(hasEntry(entries, "alpha.txt"));
        QVERIFY(hasEntry(entries, "beta.txt"));

        auto da = eng.ReadFile("alpha.txt");
        QCOMPARE(std::string(da.begin(), da.end()), c1);
        auto db = eng.ReadFile("beta.txt");
        QCOMPARE(std::string(db.begin(), db.end()), c2);
    }

    void testRemoveEntry()
    {
        std::string keep = "keep this";
        std::string drop = "delete this";
        createTempFile("src/keep.txt", keep);
        createTempFile("src/drop.txt", drop);

        auto archive = m_dir.path / "remove_test.tar.gz";
        TarGzEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "src/keep.txt").string(), "keep.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "src/drop.txt").string(), "drop.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        QVERIFY(eng.RemoveEntry("drop.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(1));
        QVERIFY(hasEntry(entries, "keep.txt"));
        QVERIFY(!hasEntry(entries, "drop.txt"));

        auto d = eng.ReadFile("keep.txt");
        QCOMPARE(std::string(d.begin(), d.end()), keep);
    }

    void testReplaceEntry()
    {
        createTempFile("src/replace_v1.txt", "version 1");
        createTempFile("src/replace_v2.txt", "version 2");

        auto archive = m_dir.path / "replace_test.tar.gz";
        TarGzEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile((baseTempDir() / "src/replace_v1.txt").string(), "data.txt"));
        QVERIFY(eng.Save());

        // Re-open and replace with v2
        QVERIFY(eng.Open(archive.string()));
        QVERIFY(eng.RemoveEntry("data.txt"));
        QVERIFY(eng.AddFile((baseTempDir() / "src/replace_v2.txt").string(), "data.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archive.string()));
        auto d = eng.ReadFile("data.txt");
        QCOMPARE(std::string(d.begin(), d.end()), "version 2");
    }

    // ── Symlink tests ─────────────────────────────────────────────────

    void testSymlinkReadFromRawArchive()
    {
        // Build a raw tar.gz that contains: real.txt + link.txt -> real.txt
        std::vector<char> tar;
        appendTarRegular(tar, "real.txt", "real file data");
        appendTarSymlink(tar, "link.txt", "real.txt");

        auto archivePath = m_dir.path / "symlink_read.tar.gz";
        QVERIFY(writeTarGz(archivePath, tar));

        TarGzEngine eng;
        QVERIFY(eng.Open(archivePath.string()));
        const auto& entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));

        bool foundSymlink = false;
        for (const auto& e : entries)
        {
            if (e.path == "link.txt")
            {
                foundSymlink = true;
                QCOMPARE(e.comment, std::string("-> real.txt"));
            }
        }
        QVERIFY(foundSymlink);
    }

    void testSymlinkExtract()
    {
#ifdef _WIN32
        QSKIP("symlink extraction not supported on Windows without elevation");
#else
        std::vector<char> tar;
        appendTarRegular(tar, "target.txt", "symlink target content");
        appendTarSymlink(tar, "link.txt", "target.txt");

        auto archivePath = m_dir.path / "symlink_extract.tar.gz";
        QVERIFY(writeTarGz(archivePath, tar));

        auto outDir = m_dir.path / "symlink_extract_out";
        TarGzEngine eng;
        QVERIFY(eng.Open(archivePath.string()));
        QVERIFY(eng.ExtractAll(outDir.string()));

        // target.txt should be a regular file
        QVERIFY(fs::exists(outDir / "target.txt"));
        QCOMPARE(readFileContents(outDir / "target.txt"), "symlink target content");

        // link.txt should be a symlink pointing to target.txt
        std::error_code ec;
        auto st = fs::symlink_status(outDir / "link.txt", ec);
        QVERIFY(!ec);
        QCOMPARE(st.type(), fs::file_type::symlink);
        auto dest = fs::read_symlink(outDir / "link.txt", ec);
        QVERIFY(!ec);
        QCOMPARE(dest.string(), std::string("target.txt"));
#endif
    }

    void testSymlinkSaveRoundTrip()
    {
#ifdef _WIN32
        QSKIP("symlink creation requires elevation on Windows");
#else
        // Create a real file and a symlink pointing to it
        auto srcReal = m_dir.path / "sym_real.txt";
        auto srcLink = m_dir.path / "sym_link.txt";
        {
            std::ofstream out(srcReal);
            out << "real content for symlink test";
        }
        std::error_code ec;
        fs::create_symlink("sym_real.txt", srcLink, ec);
        QVERIFY(!ec);

        // Create archive containing both
        auto archive = m_dir.path / "sym_roundtrip.tar.gz";
        TarGzEngine eng;
        QVERIFY(eng.Create(archive.string()));
        QVERIFY(eng.AddFile(srcReal.string(), "sym_real.txt"));
        QVERIFY(eng.AddFile(srcLink.string(), "sym_link.txt"));
        QVERIFY(eng.Save());

        // Verify symlink entry is recorded as symlink in the archive
        QVERIFY(eng.Open(archive.string()));
        const auto& entries = eng.ListContents();
        bool foundLink = false;
        for (const auto& e : entries)
        {
            if (e.path == "sym_link.txt")
            {
                foundLink = true;
                QCOMPARE(e.comment, std::string("-> sym_real.txt"));
            }
        }
        QVERIFY(foundLink);

        // Extract and verify symlink is recreated
        auto outDir = m_dir.path / "sym_roundtrip_out";
        QVERIFY(eng.ExtractAll(outDir.string()));

        auto outLink = outDir / "sym_link.txt";
        auto outLinkSt = fs::symlink_status(outLink, ec);
        QVERIFY(!ec);
        QCOMPARE(outLinkSt.type(), fs::file_type::symlink);
        auto linkDest = fs::read_symlink(outLink, ec);
        QVERIFY(!ec);
        QCOMPARE(linkDest.string(), std::string("sym_real.txt"));
#endif
    }

    void testSymlinkPreservedOnResave()
    {
        // Verify that symlink entries survive a Save() round-trip (e.g. after removing another entry)
        std::vector<char> tar;
        appendTarRegular(tar, "keep.txt", "keep this");
        appendTarRegular(tar, "drop.txt", "drop this");
        appendTarSymlink(tar, "lnk.txt", "keep.txt");

        auto archivePath = m_dir.path / "sym_preserve.tar.gz";
        QVERIFY(writeTarGz(archivePath, tar));

        TarGzEngine eng;
        QVERIFY(eng.Open(archivePath.string()));
        QVERIFY(eng.RemoveEntry("drop.txt"));
        QVERIFY(eng.Save());

        QVERIFY(eng.Open(archivePath.string()));
        const auto& entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));
        QVERIFY(hasEntry(entries, "keep.txt"));
        QVERIFY(hasEntry(entries, "lnk.txt"));

        bool foundSym = false;
        for (const auto& e : entries)
        {
            if (e.path == "lnk.txt")
            {
                foundSym = true;
                QCOMPARE(e.comment, std::string("-> keep.txt"));
            }
        }
        QVERIFY(foundSym);
    }

    // ── Hardlink tests ───────────────────────────────────────────────

    void testHardlinkResolution()
    {
        // Build a raw tar.gz with real.txt and hardlink.txt -> real.txt
        std::vector<char> tar;
        appendTarRegular(tar, "real.txt", "real file data");
        appendTarHardlink(tar, "hardlink.txt", "real.txt");

        auto archivePath = m_dir.path / "hardlink_test.tar.gz";
        QVERIFY(writeTarGz(archivePath, tar));

        TarGzEngine eng;
        QVERIFY(eng.Open(archivePath.string()));
        const auto& entries = eng.ListContents();
        QCOMPARE(entries.size(), size_t(2));
        QVERIFY(hasEntry(entries, "hardlink.txt"));

        // ReadFile on the hardlink should return the same content as the real file
        auto data = eng.ReadFile("hardlink.txt");
        QCOMPARE(std::string(data.begin(), data.end()), std::string("real file data"));
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

    void testReadNonExistent()
    {
        TarGzEngine eng;
        QVERIFY(eng.Open(m_archive.string()));
        auto data = eng.ReadFile("does_not_exist.txt");
        QVERIFY(data.empty());
    }
};

QTEST_MAIN(tst_TarGzEngine)
#include "tst_TarGzEngine.moc"
