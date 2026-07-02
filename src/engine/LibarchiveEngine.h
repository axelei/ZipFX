#ifndef ZIPFX_LIBARCHIVE_ENGINE_H
#define ZIPFX_LIBARCHIVE_ENGINE_H

#include "ArchiveEngine.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

struct archive;
struct archive_entry;

class LibarchiveEngine : public ArchiveEngine
{
public:
    using FormatRegistrar = int (*)(struct archive*);

    LibarchiveEngine(std::vector<FormatRegistrar> registrars,
                     const char* formatName,
                     bool supportsCreation = false,
                     const char* compressionMethod = nullptr);
    ~LibarchiveEngine() override;

    bool Open(std::string_view path) override;
    void Close() override;
    const std::vector<ArchiveEntry>& ListContents() override;
    bool Extract(std::string_view entryName, std::string_view destPath) override;
    bool ExtractAll(std::string_view destPath) override;
    std::vector<uint8_t> ReadFile(std::string_view entryName) override;
    std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes) override;
    bool ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer) override;
    bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) override;

    void cancelExtract() override { m_extractCancelled = true; }

    std::string_view FormatName() const override { return m_formatName; }
    bool SupportsCreation() const override { return m_supportsCreation; }
    bool IsOpen() const override { return m_isOpen; }

    // Only 7z and RAR support passphrase-encrypted entries in libarchive;
    // the other formats routed through this engine (CAB, LHA, CPIO, AR,
    // compressed tars, standalone compression, ...) have no such concept,
    // so don't advertise a password field the UI can't do anything with.
    void setPassword(std::string_view pwd) override { m_password = std::string(pwd); }
    bool SupportsEncryption() const override
    {
        return m_formatName == "7z" || m_formatName == "RAR";
    }
    // libarchive's 7z/RAR/RAR5 readers unconditionally reject encrypted
    // content regardless of passphrase (confirmed in its own source: hard
    // "not currently supported" errors, not a build-config gap) — so any
    // encrypted entry read through this engine can never succeed.
    std::string EncryptionUnavailableReason() const override
    {
        if (m_formatName == "7z" || m_formatName == "RAR")
            return "This build can't decrypt " + m_formatName +
                   " content without the 7-Zip library (7z.dll/lib7z.so) — "
                   "install 7-Zip, or place the library where ZipFX can find it.";
        return {};
    }

private:
    bool openArchive(std::string_view path);
    bool LoadEntries();
    void registerFormat(struct archive* a);

    struct archive* m_archive = nullptr;
    std::string m_path;
    std::string m_formatName;
    std::string m_compressionMethod;
    bool m_isOpen = false;
    bool m_supportsCreation = false;
    std::string m_password;
    std::vector<FormatRegistrar> m_registrars;
    std::vector<ArchiveEntry> m_entries;
    std::mutex m_archiveMutex;
    std::atomic<bool> m_extractCancelled{false};
};

#endif
