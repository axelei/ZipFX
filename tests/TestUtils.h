#ifndef ZIPFX_TEST_UTILS_H
#define ZIPFX_TEST_UTILS_H

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// Common temp base directory used by all tests
fs::path baseTempDir();
void cleanupTempDir();

namespace fs = std::filesystem;

// Create a file with given content in a temp directory
fs::path createTempFile(const std::string& name, const std::string& content);

// Create a unique temp directory that cleans up on destruction
struct TempDir
{
    fs::path path;
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Assert that a vector contains exactly one entry with the given name
bool hasEntry(const std::vector<struct ArchiveEntry>& entries, const std::string& name);

// Read entire file into string
std::string readFileContents(const fs::path& path);

#endif
