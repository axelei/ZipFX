#include "CliHandler.h"

#include "engine/ArchiveEngineFactory.h"
#include "engine/ArchiveEngine.h"
#include "engine/ArchiveEntry.h"
#include "engine/Bit7zEngine.h"
#include "version.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

// ── helpers ────────────────────────────────────────────────────────────
static int doList(const std::string& path)
{
    auto engine = ArchiveEngineFactory::CreateForFile(path);
    if (!engine || !engine->Open(path))
    {
        std::cerr << "Error: cannot open " << path << std::endl;
        return 1;
    }

    auto entries = engine->ListContents();
    uint64_t totalSize = 0, totalPacked = 0;
    int files = 0;

    std::cout << std::left
              << std::setw(50) << "Name"
              << std::setw(12) << "Size"
              << std::setw(12) << "Packed"
              << std::setw(8)  << "CRC"
              << std::endl;
    std::cout << std::string(82, '-') << std::endl;

    for (const auto& e : entries)
    {
        if (e.isDirectory)
        {
            std::cout << std::left << std::setw(50) << (e.name + "/")
                      << std::setw(12) << "-"
                      << std::setw(12) << "-"
                      << std::setw(8)  << "-"
                      << std::endl;
            continue;
        }

        std::cout << std::left << std::setw(50) << e.name
                  << std::right
                  << std::setw(12) << e.size
                  << std::setw(12) << e.packedSize
                  << std::setw(8)  << [&]() -> std::string {
                      if (!e.crc) return "-";
                      std::ostringstream ss;
                      ss << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << e.crc;
                      return ss.str();
                  }()
                  << std::left
                  << std::endl;
        files++;
        totalSize += e.size;
        totalPacked += e.packedSize;
    }

    std::cout << std::string(82, '-') << std::endl;
    std::cout << files << " files, "
              << totalSize << " bytes (" << totalPacked << " packed)"
              << std::endl;

    engine->Close();
    return 0;
}

static int doExtract(const std::string& path, const std::string& outDir)
{
    auto engine = ArchiveEngineFactory::CreateForFile(path);
    if (!engine || !engine->Open(path))
    {
        std::cerr << "Error: cannot open " << path << std::endl;
        return 1;
    }

    fs::create_directories(outDir);

    if (!engine->ExtractAll(outDir))
    {
        std::cerr << "Error: extraction failed" << std::endl;
        engine->Close();
        return 1;
    }

    std::cout << "Extracted to " << outDir << std::endl;
    engine->Close();
    return 0;
}

static int doCreate(const std::string& path, const std::vector<std::string>& sources,
                    int compressionLevel, const std::string& password,
                    bool encryptHeaders, uint64_t volumeSize)
{
    // Detect format from extension
    auto engine = ArchiveEngineFactory::CreateForFile(path);
    if (!engine)
    {
        // Try by format name
        auto dot = path.rfind('.');
        if (dot != std::string::npos)
        {
            std::string ext = path.substr(dot);
            if (ext == ".7z")
                engine = ArchiveEngineFactory::CreateForFormat("7z");
            else if (ext == ".zip")
                engine = ArchiveEngineFactory::CreateForFormat("zip");
        }
    }
    if (!engine)
    {
        std::cerr << "Error: unsupported format for " << path << std::endl;
        return 1;
    }

    // Apply Bit7zEngine settings
    auto* bit7z = dynamic_cast<Bit7zEngine*>(engine.get());
    if (bit7z)
    {
        if (!password.empty()) bit7z->setPassword(password);
        if (encryptHeaders) bit7z->setEncryptHeaders(true);
        if (volumeSize > 0) bit7z->setVolumeSize(volumeSize * 1024 * 1024);
    }

    if (!engine->Create(path))
    {
        std::cerr << "Error: cannot create " << path << std::endl;
        return 1;
    }

    int total = 0;
    for (const auto& src : sources)
    {
        fs::path p(src);
        if (fs::is_directory(p))
        {
            for (const auto& de : fs::recursive_directory_iterator(p))
                if (de.is_regular_file()) total++;
        }
        else if (fs::is_regular_file(p))
        {
            total++;
        }
    }

    int count = 0;
    for (const auto& src : sources)
    {
        fs::path p(src);
        if (fs::is_directory(p))
        {
            for (const auto& de : fs::recursive_directory_iterator(p))
            {
                if (de.is_regular_file())
                {
                    fs::path rel = de.path().lexically_relative(p);
                    engine->AddFile(de.path().string(), rel.generic_string());
                    count++;
                    if (count % 50 == 0)
                        std::cout << "\rAdding... " << count << "/" << total << std::flush;
                }
            }
        }
        else if (fs::is_regular_file(p))
        {
            engine->AddFile(p.string(), p.filename().string());
            count++;
            if (count % 50 == 0)
                std::cout << "\rAdding... " << count << "/" << total << std::flush;
        }
    }

    if (total > 0)
        std::cout << "\rAdding... " << total << "/" << total << std::endl;

    if (!engine->Save())
    {
        std::cerr << "Error: failed to save archive" << std::endl;
        engine->Close();
        return 1;
    }

    std::cout << "Created " << path << " (" << total << " files)" << std::endl;
    engine->Close();
    return 0;
}

static int doTest(const std::string& path)
{
    auto engine = ArchiveEngineFactory::CreateForFile(path);
    if (!engine || !engine->Open(path))
    {
        std::cerr << "Error: cannot open " << path << std::endl;
        return 1;
    }

    bool ok = engine->TestIntegrity();
    engine->Close();

    std::cout << (ok ? "Integrity check passed" : "Integrity check FAILED") << std::endl;
    return ok ? 0 : 1;
}

static int doInfo(const std::string& path)
{
    auto engine = ArchiveEngineFactory::CreateForFile(path);
    if (!engine || !engine->Open(path))
    {
        std::cerr << "Error: cannot open " << path << std::endl;
        return 1;
    }

    auto entries = engine->ListContents();
    uint64_t totalSize = 0, totalPacked = 0;
    int files = 0, folders = 0;

    for (const auto& e : entries)
    {
        if (e.isDirectory) { folders++; continue; }
        files++;
        totalSize += e.size;
        totalPacked += e.packedSize;
    }

    std::cout << "Archive: " << path << std::endl;
    std::cout << "Format: " << engine->FormatName() << std::endl;
    std::cout << "Files: " << files << ", Folders: " << folders << std::endl;
    std::cout << "Size: " << totalSize << " bytes" << std::endl;
    std::cout << "Packed: " << totalPacked << " bytes" << std::endl;
    if (totalSize > 0)
        std::cout << "Ratio: " << (totalPacked * 100 / totalSize) << "%" << std::endl;

    engine->Close();
    return 0;
}

// ── CLI entry point ────────────────────────────────────────────────────
int runCli(int argc, char* argv[])
{
    CLI::App app{
        "ZipFX v" ZIPFX_VERSION " — Multiplatform archiver for power users" };

    app.require_subcommand(1);

    // list
    auto* listCmd = app.add_subcommand("list", "List archive contents");
    std::string listArchive;
    listCmd->add_option("archive", listArchive, "Path to the archive")->required();

    // extract
    auto* extractCmd = app.add_subcommand("extract", "Extract archive");
    std::string extractArchive;
    std::string extractDir;
    extractCmd->add_option("archive", extractArchive, "Path to the archive")->required();
    extractCmd->add_option("-o,--output", extractDir, "Output directory (default: archive name without extension)");

    // create
    auto* createCmd = app.add_subcommand("create", "Create archive");
    std::string createPath;
    std::vector<std::string> createSources;
    int compressionLevel = 6;
    std::string password;
    bool encryptHeaders = false;
    uint64_t volumeSize = 0;
    createCmd->add_option("archive", createPath, "Path to the new archive")->required();
    createCmd->add_option("sources", createSources, "Files and folders to compress")->required();
    createCmd->add_option("-c,--compression", compressionLevel, "Compression level (0-9)");
    createCmd->add_option("-p,--password", password, "Encryption password");
    createCmd->add_flag("--encrypt-headers", encryptHeaders, "Encrypt file names (7z only)");
    createCmd->add_option("-v,--volume", volumeSize, "Volume size in MB (0 = none)");

    // test
    auto* testCmd = app.add_subcommand("test", "Test archive integrity");
    std::string testArchive;
    testCmd->add_option("archive", testArchive, "Path to the archive")->required();

    // info
    auto* infoCmd = app.add_subcommand("info", "Show archive information");
    std::string infoArchive;
    infoCmd->add_option("archive", infoArchive, "Path to the archive")->required();

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        return app.exit(e);
    }

    // Dispatch
    if (*listCmd)
        return doList(listArchive);

    if (*extractCmd)
    {
        auto dir = extractDir.empty()
            ? fs::path(extractArchive).stem().string()
            : extractDir;
        return doExtract(extractArchive, dir);
    }

    if (*createCmd)
        return doCreate(createPath, createSources,
                        compressionLevel, password,
                        encryptHeaders, volumeSize);

    if (*testCmd)
        return doTest(testArchive);

    if (*infoCmd)
        return doInfo(infoArchive);

    return 0;
}
