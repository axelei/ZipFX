#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "engine/WadEngine.h"
#include "engine/PakEngine.h"
#include "engine/GrpEngine.h"
#include "engine/HogEngine.h"
#include "engine/VpkEngine.h"
#include "engine/GobEngine.h"
#include "engine/RffEngine.h"
#include "engine/BigEngine.h"
#include "engine/PodEngine.h"
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

static void testFormat(const char* label, const char* ext,
                       std::function<std::unique_ptr<ArchiveEngine>()> createEng)
{
    TempDir dir;
    std::string contentA = "Hello World!";
    std::string contentB = "File B content.";
    std::string contentC = "Third file.";

    createTempFile("src/a.txt", contentA);
    createTempFile("src/b.txt", contentB);
    createTempFile("src/c.txt", contentC);

    // ── Create + Add + Save ──
    {
        TEST((std::string(label) + " create").c_str());
        auto eng = createEng();
        auto archive = (dir.path / (std::string("test") + ext)).string();
        if (!eng->Create(archive)) { FAIL("Create returned false"); }
        if (!eng->AddFile((baseTempDir() / "src/a.txt").string(), "a.txt"))
            { FAIL("AddFile a.txt failed"); }
        if (!eng->AddFile((baseTempDir() / "src/b.txt").string(), "b.txt"))
            { FAIL("AddFile b.txt failed"); }
        if (!eng->Save()) { FAIL("Save failed"); }
        PASS();
    }

    // ── Open + List + Read ──
    {
        TEST((std::string(label) + " open").c_str());
        auto eng = createEng();
        auto archive = (dir.path / (std::string("test") + ext)).string();
        if (!eng->Open(archive)) { FAIL("Open failed"); }
        auto entries = eng->ListContents();
        if (entries.size() != 2) { FAIL("Expected 2 entries"); }
        if (!hasEntryVec(entries, "a.txt"))
            { FAIL("Missing a.txt"); }
        if (!hasEntryVec(entries, "b.txt"))
            { FAIL("Missing b.txt"); }

        auto data = eng->ReadFile("a.txt");
        if (data.empty()) { FAIL("ReadFile returned empty"); }
        if (std::string(data.begin(), data.end()) != contentA)
            { FAIL("Content mismatch"); }
        PASS();
    }

    // ── Remove + Save ──
    {
        TEST((std::string(label) + " remove").c_str());
        auto eng = createEng();
        auto archive = (dir.path / (std::string("test") + ext)).string();
        if (!eng->Open(archive)) { FAIL("Open failed"); }
        if (!eng->AddFile((baseTempDir() / "src/c.txt").string(), "c.txt"))
            { FAIL("AddFile c.txt failed"); }
        if (!eng->Save()) { FAIL("Save after add failed"); }
        if (!eng->Open(archive)) { FAIL("Re-open failed"); }
        if (!eng->RemoveEntry("c.txt")) { FAIL("RemoveEntry failed"); }
        if (!eng->Save()) { FAIL("Save after remove failed"); }
        if (!eng->Open(archive)) { FAIL("Re-open after remove failed"); }
        auto entries = eng->ListContents();
        if (entries.size() != 2) { FAIL("Expected 2 entries after remove"); }
        if (hasEntryVec(entries, "c.txt")) { FAIL("c.txt still present"); }
        PASS();
    }

    // ── TestIntegrity ──
    {
        TEST((std::string(label) + " integrity").c_str());
        auto eng = createEng();
        auto archive = (dir.path / (std::string("test") + ext)).string();
        if (!eng->Open(archive)) { FAIL("Open failed"); }
        if (!eng->TestIntegrity()) { FAIL("TestIntegrity failed"); }
        PASS();
    }

    // ── Open non-existent ──
    {
        TEST((std::string(label) + " open_nonexistent").c_str());
        auto eng = createEng();
        if (eng->Open((dir.path / "nope").string())) { FAIL("Should not open"); }
        PASS();
    }

next:
    ;
}

int main()
{
    printf("FlatArchiveEngine Tests\n");
    printf("=======================\n\n");

    testFormat("WAD", ".wad", []() { return std::make_unique<WadEngine>(); });
    testFormat("PAK", ".pak", []() { return std::make_unique<PakEngine>(); });
    testFormat("GRP", ".grp", []() { return std::make_unique<GrpEngine>(); });
    testFormat("HOG", ".hog", []() { return std::make_unique<HogEngine>(); });
    testFormat("VPK", ".vpk", []() { return std::make_unique<VpkEngine>(); });
    testFormat("GOB", ".gob", []() { return std::make_unique<GobEngine>(); });
    testFormat("RFF", ".rff", []() { return std::make_unique<RffEngine>(); });
    testFormat("BIG", ".big", []() { return std::make_unique<BigEngine>(); });
    testFormat("POD", ".pod", []() { return std::make_unique<PodEngine>(); });

    printf("\n%d / %d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
