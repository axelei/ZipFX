#include "TestUtils.h"

#include "engine/ArchiveEntry.h"

#include <cstdio>
#include <fstream>
#include <random>

fs::path baseTempDir()
{
    auto tmp = fs::temp_directory_path() / "zipfx-tests";
    fs::create_directories(tmp);
    return tmp;
}

static std::string uniqueId()
{
    static std::mt19937 rng(std::random_device{}());
    return std::to_string(rng());
}

fs::path createTempFile(const std::string& name, const std::string& content)
{
    auto path = baseTempDir() / name;
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return path;
}

TempDir::TempDir()
{
    path = baseTempDir() / uniqueId();
    fs::create_directories(path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    fs::remove_all(path, ec);
}

bool hasEntry(const std::vector<ArchiveEntry>& entries, const std::string& name)
{
    for (const auto& e : entries)
        if (e.path == name) return true;
    return false;
}

std::string readFileContents(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    auto size = in.tellg();
    in.seekg(0);
    std::string buf(static_cast<size_t>(size), '\0');
    in.read(buf.data(), size);
    return buf;
}
