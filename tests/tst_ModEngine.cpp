#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#include "engine/ModEngine.h"
#include "engine/ArchiveEngineFactory.h"
#include "engine/FileSignature.h"
#include "TestUtils.h"

namespace fs = std::filesystem;

static int total = 0;
static int passed = 0;

#define TEST(name) do { printf("  %s ... ", name); fflush(stdout); total++; } while(0)
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return false; } while(0)

// ── Minimal valid Protracker MOD file ──────────────────────────────
// Layout: 20 name + 31×30 sample slots + 1 song len + 1 restart +
//         128 orders + 4 magic = 1084 bytes header,
//         then 1024 bytes pattern 0, then 2 bytes sample data.
static std::vector<uint8_t> createMod()
{
    std::vector<uint8_t> d;
    auto w = [&](const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        d.insert(d.end(), b, b + n);
    };
    uint8_t name[20] = {};
    w(name, 20);
    for (int i = 0; i < 31; ++i) {
        uint8_t sh[30] = {};
        if (i == 0) {
            const char* sname = "Kick Drum";
            std::memcpy(sh, sname, std::strlen(sname));
            sh[22] = 0; sh[23] = 1;
            sh[24] = 0; sh[25] = 64;
            sh[26] = 0; sh[27] = 0; sh[28] = 0; sh[29] = 0;
        }
        w(sh, 30);
    }
    uint8_t songLen = 1; w(&songLen, 1);
    uint8_t restart = 0; w(&restart, 1);
    uint8_t orders[128];
    std::memset(orders, 0xFF, 128);
    orders[0] = 0;
    w(orders, 128);
    uint8_t sig[4] = {'M', '.', 'K', '.'};
    w(sig, 4);
    uint8_t pat[1024] = {};
    w(pat, 1024);
    uint8_t silence[2] = {};
    w(silence, 2);
    return d;
}

static fs::path writeFile(const fs::path& dir, const char* name,
                          const std::vector<uint8_t>& data)
{
    fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return p;
}

static bool testMod(const fs::path& modPath)
{
    auto eng = ArchiveEngineFactory::CreateForFile(modPath.string());
    if (!eng) { TEST("factory"); FAIL("null"); }
    TEST("factory"); PASS();

    if (!eng->Open(modPath.string())) { TEST("open"); FAIL("Open failed"); }
    TEST("open"); PASS();

    auto entries = eng->ListContents();
    TEST("list");
    if (entries.empty()) FAIL("no entries");
    if (entries[0].name.find("Kick Drum") == std::string::npos)
        FAIL("expected sample name in entry name");
    PASS();

    TEST("format name");
    if (eng->FormatName().find("Tracker") == std::string_view::npos)
        FAIL("FormatName should contain 'Tracker'");
    PASS();

    TEST("ReadFile");
    auto data = eng->ReadFile(entries[0].name);
    if (data.empty()) FAIL("empty");
    if (data.size() < 44 || data[0] != 'R' || data[1] != 'I' ||
        data[2] != 'F' || data[3] != 'F')
        FAIL("invalid WAV");
    PASS();

    TEST("archiveComment");
    auto comment = eng->archiveComment();
    if (comment.find("Type:") == std::string::npos)
        FAIL("expected Type: in comment");
    PASS();

    TEST("TestIntegrity");
    if (!eng->TestIntegrity(nullptr, nullptr)) FAIL("failed");
    PASS();

    eng->Close();
    return true;
}

int main()
{
    printf("ModEngine tests:\n\n");

    // 1. Edge cases
    {
        TEST("non-existent file");
        auto eng = std::make_unique<ModEngine>();
        if (eng->Open("nope.mod")) FAIL("should not open non-existent");  // return
        if (eng->IsOpen()) FAIL("IsOpen expected false");
        PASS();
    }

    {
        TempDir dir;
        auto p = dir.path / "bogus.mod";
        {
            std::ofstream out(p, std::ios::binary);
            uint8_t junk[64] = {0xFF,0xFF,0xFF,0xFF,0xDE,0xAD,0xBE,0xEF};
            out.write(reinterpret_cast<const char*>(junk), sizeof(junk));
        }
        TEST("invalid data");
        auto eng = std::make_unique<ModEngine>();
        if (eng->Open(p.string())) FAIL("should not open invalid");
        if (eng->IsOpen()) FAIL("IsOpen expected false");
        PASS();
    }

    // 2. Minimal MOD (self-contained, always works)
    {
        TempDir dir;
        auto modPath = writeFile(dir.path, "minimal.mod", createMod());
        if (testMod(modPath)) { /* all sub-tests counted inside */ }

        TEST("MOD magic detection (M.K. at offset 1080, not in first 32)");
        auto sig = FileSignature::Detect(modPath.string());
        if (sig == ArchiveType::Mod) FAIL("should not detect by magic");
        PASS();
    }

    printf("\n%d / %d passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
