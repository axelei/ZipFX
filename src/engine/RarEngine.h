#pragma once

#include "ArchiveEngine.h"
#include <memory>
#include <string>
#include <vector>

// ── RarEngine ──────────────────────────────────────────────────────────────
//
// RAR read/write engine.
//
// Reading: delegates to Bit7zEngine (preferred, requires 7z.dll) or
//          LibarchiveEngine (fallback, read-only).
//
// Writing: shells out to WinRAR's rar.exe / rar binary.  Files queued via
//          AddFile() are staged in a QTemporaryDir that mirrors the desired
//          archive-path layout, then committed with:
//
//    rar a -ep1 -r -m<level> [-p<pwd>] [-idq] <output.rar> <tmpdir>/
//
//  -ep1 strips the tmpdir prefix so archive paths come from entry names.
//
// RemoveEntry: rar d <archive> <entry>
// RenameEntry: not supported via rar.exe CLI (returns false).

class RarEngine final : public ArchiveEngine
{
public:
    RarEngine();
    ~RarEngine() override;

    // ── Static helpers ─────────────────────────────────────────────────────
    // Returns the path to rar.exe / rar, or empty if not installed.
    // Result is cached; call resetFindCache() after an auto-install.
    static std::string findRarExe();
    static bool        isAvailable() { return !findRarExe().empty(); }
    static void        resetFindCache();

    // Auto-install support (winget on Windows, brew on macOS).
    static bool                     canAutoInstall();
    static std::string              autoInstallDescription(); // e.g. "brew install rar"
    // args[0] = program, args[1..] = arguments
    static std::vector<std::string> autoInstallArgs();

    // ── ArchiveEngine interface ────────────────────────────────────────────
    bool Open(std::string_view path) override;
    bool Create(std::string_view path) override;
    void Close() override;
    bool IsOpen() const override { return m_isOpen; }

    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes) override;

    bool AddFile(std::string_view srcPath, std::string_view archivePath) override;
    bool RemoveEntry(std::string_view entryName) override;
    bool Save() override;

    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    std::string_view FormatName()     const override { return "RAR"; }
    bool SupportsCreation()           const override { return isAvailable(); }
    bool SupportsViewFile()           const override;
    std::string ViewUnsupportedReason() const override;
    void setPassword(std::string_view pwd) override { m_password = std::string(pwd); }

private:
    // pending files for the next Save() call: {srcPath, archiveEntryPath}
    struct PendingFile { std::string srcPath; std::string entryPath; };

    std::string                   m_path;
    bool                          m_isOpen     = false;
    std::string                   m_password;
    std::vector<PendingFile>      m_pending;
    std::unique_ptr<ArchiveEngine> m_reader;          // Bit7zEngine (primary)
    std::unique_ptr<ArchiveEngine> m_fallbackReader;   // LibarchiveEngine (fallback)

    void initReader(std::string_view path);
    int  rarCompressionLevel() const; // translate 0-9 → rar 0-5
};
