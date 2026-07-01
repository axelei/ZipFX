#ifndef ZIPFX_ARCHIVE_ENGINE_H
#define ZIPFX_ARCHIVE_ENGINE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ArchiveEntry.h"

class ArchiveEngine
{
public:
    virtual ~ArchiveEngine() = default;

    virtual bool Open(std::string_view path) = 0;
    virtual bool Create(std::string_view path) { return false; }
    virtual void Close() = 0;

    virtual const std::vector<ArchiveEntry>& ListContents() = 0;
    virtual bool Extract(std::string_view entryName, std::string_view destPath);
    virtual bool ExtractAll(std::string_view destPath) = 0;
    virtual std::vector<uint8_t> ReadFile(std::string_view entryName) = 0;
    virtual std::vector<uint8_t> ReadFilePartial(std::string_view entryName, size_t maxBytes);

    // Stream file data to consumer without loading the whole file into memory.
    // consumer(data, len) is called for each chunk; return false to abort early.
    // Default implementation falls back to ReadFile for engines that don't override.
    using StreamConsumer = std::function<bool(const uint8_t* data, size_t len)>;
    virtual bool ReadFileStreamed(std::string_view entryName, const StreamConsumer& consumer);

    virtual bool AddFile(std::string_view srcPath, std::string_view archivePath) { return false; }
    virtual bool RemoveEntry(std::string_view entryName) { return false; }
    virtual bool RenameEntry(std::string_view entryName, std::string_view newName);
    virtual bool Save() { return false; }

    virtual bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) = 0;

    void cancelSave() { m_saveCancelled = true; }
    void resetSaveCancel() { m_saveCancelled = false; }
    bool isSaveCancelled() const { return m_saveCancelled; }
    virtual void cancelExtract() {} // engines may override to abort extraction
    void cancelOpen() { m_openCancelled = true; }
    void resetOpenCancel() { m_openCancelled = false; }
    bool isOpenCancelled() const { return m_openCancelled; }

    struct ExtractProgressInfo {
        uint64_t bytesProcessed = 0;
        uint64_t totalBytes = 0;
        std::string fileName;
    };
    using ExtractProgressCb = std::function<void(const ExtractProgressInfo&)>;
    void setExtractProgressCb(ExtractProgressCb cb) { m_extractProgressCb = std::move(cb); }

    struct SaveProgressInfo {
        int currentFile = 0;
        int totalFiles = 0;
        uint64_t bytesProcessed = 0;
        uint64_t totalBytes = 0;
        std::string fileName;
    };
    using SaveProgressCb = std::function<void(const SaveProgressInfo&)>;
    void setSaveProgressCb(SaveProgressCb cb) { m_saveProgressCb = std::move(cb); }

    struct OpenProgressInfo {
        int64_t currentBytes = 0;
        int64_t totalBytes = 0;
        std::string fileName;
    };
    using OpenProgressCb = std::function<void(const OpenProgressInfo&)>;
    void setOpenProgressCb(OpenProgressCb cb) { m_openProgressCb = std::move(cb); }

    virtual std::string archiveComment() const { return {}; }
    virtual bool setArchiveComment(std::string_view comment) { return false; }
    virtual bool setEntryComment(std::string_view entryName, std::string_view comment) { return false; }
    virtual bool supportsArchiveComment() const { return false; }

    virtual void setCompressionLevel(int level) { m_compressionLevel = level; }
    virtual int compressionLevel() const { return m_compressionLevel; }

    virtual void setPassword(std::string_view /*pwd*/) {}
    virtual bool SupportsEncryption() const { return false; }

    virtual std::string_view FormatName() const = 0;
    virtual bool SupportsCreation() const = 0;
    virtual bool IsOpen() const = 0;
    virtual bool SupportsViewFile() const { return true; }
    virtual std::string ViewUnsupportedReason() const { return {}; }

    static bool isSafeEntryName(const std::string& entryName)
    {
        if (entryName.empty() || entryName.find("..") != std::string::npos)
            return false;
        if (entryName.size() > 0 && entryName[0] == '/')
            return false;
        return true;
    }

protected:
    int m_compressionLevel = 6; // 0=store, 9=max
    std::atomic<bool> m_saveCancelled{false};
    std::atomic<bool> m_openCancelled{false};
    SaveProgressCb m_saveProgressCb;
    ExtractProgressCb m_extractProgressCb;
    OpenProgressCb m_openProgressCb;
};

#endif
