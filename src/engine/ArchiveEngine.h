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

    virtual bool AddFile(std::string_view srcPath, std::string_view archivePath) { return false; }
    virtual bool RemoveEntry(std::string_view entryName) { return false; }
    virtual bool RenameEntry(std::string_view entryName, std::string_view newName);
    virtual bool Save() { return false; }

    virtual bool TestIntegrity(
        std::function<void(int current, int total)> progressCallback = nullptr,
        std::function<bool()> cancelFlag = nullptr) = 0;

    void cancelSave() { m_saveCancelled = true; }
    bool isSaveCancelled() const { return m_saveCancelled; }
    virtual void cancelExtract() {} // engines may override to abort extraction

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

    virtual std::string archiveComment() const { return {}; }
    virtual bool setArchiveComment(std::string_view comment) { return false; }
    virtual bool setEntryComment(std::string_view entryName, std::string_view comment) { return false; }

    virtual void setCompressionLevel(int level) { m_compressionLevel = level; }
    virtual int compressionLevel() const { return m_compressionLevel; }

    virtual std::string_view FormatName() const = 0;
    virtual bool SupportsCreation() const = 0;
    virtual bool IsOpen() const = 0;

protected:
    int m_compressionLevel = 6; // 0=store, 9=max
    std::atomic<bool> m_saveCancelled{false};
    SaveProgressCb m_saveProgressCb;
    ExtractProgressCb m_extractProgressCb;
};

#endif
