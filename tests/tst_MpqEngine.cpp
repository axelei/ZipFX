#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "engine/MpqEngine.h"
#include "engine/ArchiveEntry.h"
#include "TestUtils.h"

namespace fs = std::filesystem;

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %s ... ", name); fflush(stdout); total++; } while(0)
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); goto next; } while(0)

static bool hasEntryVec(const std::vector<ArchiveEntry>& entries, const std::string& name)
{
    for (const auto& e : entries)
        if (e.path == name) return true;
    return false;
}

int main()
{
    printf("MpqEngine Tests\n");
    printf("===============\n\n");

    TempDir dir;
    std::string contentA = "Hello World!";
    std::string contentB = "File B content.";
    std::string contentC = "Third file.";

    createTempFile("src/a.txt", contentA);
    createTempFile("src/b.txt", contentB);
    createTempFile("src/c.txt", contentC);

    // ── Create + Add + Save ──
    {
        TEST("MPQ create");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Create(archive)) { FAIL("Create returned false"); }
        if (!eng.AddFile((baseTempDir() / "src/a.txt").string(), "a.txt"))
            { FAIL("AddFile a.txt failed"); }
        if (!eng.AddFile((baseTempDir() / "src/b.txt").string(), "b.txt"))
            { FAIL("AddFile b.txt failed"); }
        if (!eng.Save()) { FAIL("Save failed"); }
        PASS();
    }

    // ── Open + List + Read ──
    {
        TEST("MPQ open");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        auto entries = eng.ListContents();
        if (entries.size() != 2) { FAIL("Expected 2 entries"); }
        if (!hasEntryVec(entries, "a.txt"))
            { FAIL("Missing a.txt"); }
        if (!hasEntryVec(entries, "b.txt"))
            { FAIL("Missing b.txt"); }

        auto data = eng.ReadFile("a.txt");
        if (data.empty()) { FAIL("ReadFile returned empty"); }
        if (std::string(data.begin(), data.end()) != contentA)
            { FAIL("Content mismatch"); }
        PASS();
    }

    // ── Remove + Save ──
    {
        TEST("MPQ remove");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        if (!eng.AddFile((baseTempDir() / "src/c.txt").string(), "c.txt"))
            { FAIL("AddFile c.txt failed"); }
        if (!eng.Save()) { FAIL("Save after add failed"); }
        if (!eng.Open(archive)) { FAIL("Re-open failed"); }
        if (!eng.RemoveEntry("c.txt")) { FAIL("RemoveEntry failed"); }
        if (!eng.Save()) { FAIL("Save after remove failed"); }
        if (!eng.Open(archive)) { FAIL("Re-open after remove failed"); }
        auto entries = eng.ListContents();
        if (entries.size() != 2) { FAIL("Expected 2 entries after remove"); }
        if (hasEntryVec(entries, "c.txt")) { FAIL("c.txt still present"); }
        PASS();
    }

    // ── Rename + Save ──
    {
        TEST("MPQ rename");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        if (!eng.RenameEntry("a.txt", "a_renamed.txt"))
            { FAIL("RenameEntry failed"); }
        if (!eng.Save()) { FAIL("Save after rename failed"); }
        if (!eng.Open(archive)) { FAIL("Re-open after rename failed"); }
        auto entries = eng.ListContents();
        if (entries.size() != 2) { FAIL("Expected 2 entries after rename"); }
        if (!hasEntryVec(entries, "a_renamed.txt"))
            { FAIL("Missing a_renamed.txt"); }
        if (hasEntryVec(entries, "a.txt"))
            { FAIL("a.txt still present after rename"); }
        PASS();
    }

    // ── Extract ──
    {
        TEST("MPQ extract");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        auto dest = (dir.path / "extracted" / "a_renamed.txt").string();
        if (!eng.Extract("a_renamed.txt", dest))
            { FAIL("Extract failed"); }
        auto extracted = readFileContents(dest);
        if (extracted != contentA)
            { FAIL("Extracted content mismatch"); }
        PASS();
    }

    // ── ExtractAll ──
    {
        TEST("MPQ extract_all");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        auto dest = (dir.path / "extracted_all").string();
        if (!eng.ExtractAll(dest))
            { FAIL("ExtractAll failed"); }
        auto extractedA = readFileContents(fs::path(dest) / "a_renamed.txt");
        if (extractedA != contentA)
            { FAIL("Extracted a_renamed.txt content mismatch"); }
        auto extractedB = readFileContents(fs::path(dest) / "b.txt");
        if (extractedB != contentB)
            { FAIL("Extracted b.txt content mismatch"); }
        PASS();
    }

    // ── TestIntegrity ──
    {
        TEST("MPQ integrity");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpq").string();
        if (!eng.Open(archive)) { FAIL("Open failed"); }
        if (!eng.TestIntegrity()) { FAIL("TestIntegrity failed"); }
        PASS();
    }

    // ── Open non-existent ──
    {
        TEST("MPQ open_nonexistent");
        MpqEngine eng;
        if (eng.Open((dir.path / "nope").string())) { FAIL("Should not open"); }
        PASS();
    }

    // ── Create with .mpk extension ──
    {
        TEST("MPQ create_mpk");
        MpqEngine eng;
        auto archive = (dir.path / "test.mpk").string();
        if (!eng.Create(archive)) { FAIL("Create returned false"); }
        if (!eng.AddFile((baseTempDir() / "src/c.txt").string(), "c.txt"))
            { FAIL("AddFile c.txt failed"); }
        if (!eng.Save()) { FAIL("Save failed"); }
        if (!eng.Open(archive)) { FAIL("Re-open failed"); }
        auto entries = eng.ListContents();
        if (entries.size() != 1) { FAIL("Expected 1 entry"); }
        PASS();
    }

next:
    printf("\n%d / %d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
