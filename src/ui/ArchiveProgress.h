#ifndef ZIPFX_ARCHIVEPROGRESS_H
#define ZIPFX_ARCHIVEPROGRESS_H

#include <QString>
#include <cstdint>

class ArchiveEngine;
class QProgressDialog;
class QWidget;
struct ArchiveEntry;
struct ProgressInfo;

namespace ArchiveProgress {

enum class SaveResult { Ok, Cancelled, Failed };

// Extract a single entry with threaded progress reporting.
// progressDlg must be set up and visible before calling.
bool extractFile(ArchiveEngine* engine, const ArchiveEntry& entry,
                  const QString& destFile, ProgressInfo& pi,
                  uint64_t baseBytes, QProgressDialog* progressDlg);

// Save with byte-rate ETA progress.
// progressDlg must be set up and visible before calling.
// Closes and deletes progressDlg on exit.
SaveResult save(ArchiveEngine* engine, QProgressDialog* progressDlg,
                 QWidget* parent);

// Lightweight modal save with indeterminate progress bar.
// Creates a local QDialog (no user cancellation).
bool runSave(ArchiveEngine* engine, const QString& label,
              QWidget* parent);

} // namespace ArchiveProgress

#endif
