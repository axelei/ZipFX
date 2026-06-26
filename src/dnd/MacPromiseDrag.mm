#include <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <memory>
#include <atomic>
#include <vector>
#include <string>
#include <fstream>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QWidget>
#include <QtCore/QString>

#include "engine/ArchiveEngine.h"
#include "ui/ProgressInfo.h"

@interface BatchDelegate : NSObject <NSFilePromiseProviderDelegate, NSDraggingSource>
@property (readwrite) ArchiveEngine* engine;
@property (copy) NSArray* archivePaths;
@property (copy) NSArray* displayPaths;
@property (readwrite) QWidget* parentWidget;
@end

@implementation BatchDelegate
- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)p fileNameForType:(NSString*)t
{
    return @"ZipFX_Export.extracted";
}

// Called by the OS on a background queue.
//
// Design: never dispatch_sync to the main queue — it deadlocks against the Cocoa
// drag run-loop. Use dispatch_async(main_queue) for all UI, and
// dispatch_async(global_queue) for extraction so the main thread stays free.
//
// Progress: we use engine->Extract() + setExtractProgressCb() which gives true
// per-chunk streaming progress for ZipEngine and Bit7zEngine (256 KB chunks).
// Engines that fall back to the default Extract() (LibarchiveEngine, flat engines)
// call the callback once per file instead, so progress is file-granular there.
// Either way, ProgressInfo computes EWMA speed and ETA across the whole session.
- (void)filePromiseProvider:(NSFilePromiseProvider*)p writePromiseToURL:(NSURL*)url completionHandler:(void (^)(NSError*))h
{
    NSString* destDir = [[url path] stringByDeletingLastPathComponent];
    NSFileManager* fm = [NSFileManager defaultManager];

    // ── Conflict scan (background queue, no UI) ──────────────────────────────
    NSMutableArray<NSString*>* existing = [NSMutableArray array];
    for (NSUInteger i = 0; i < self.displayPaths.count; i++) {
        NSString* dest = [destDir stringByAppendingPathComponent:self.displayPaths[i]];
        if ([fm fileExistsAtPath:dest])
            [existing addObject:self.displayPaths[i]];
    }

    // ── Convert Obj-C arrays → C++ for safe cross-block use ──────────────────
    int fileCount = (int)self.archivePaths.count;
    auto archPaths = std::make_shared<std::vector<std::string>>();
    auto dispPaths = std::make_shared<std::vector<std::string>>();
    archPaths->reserve(fileCount);
    dispPaths->reserve(fileCount);
    for (int i = 0; i < fileCount; i++) {
        archPaths->push_back(std::string([self.archivePaths[i] UTF8String]));
        dispPaths->push_back(std::string([self.displayPaths[i] UTF8String]));
    }
    std::string destDirStr([destDir UTF8String]);
    ArchiveEngine* engine = self.engine;
    QWidget* parentWidget = self.parentWidget;

    // ── Collect uncompressed sizes for byte-accurate progress + ETA ──────────
    auto allEntries = engine->ListContents();
    uint64_t totalBytes = 0;
    auto fileSizes = std::make_shared<std::vector<uint64_t>>(fileCount, 0);
    for (int i = 0; i < fileCount; i++) {
        for (const auto& e : allEntries) {
            if (e.path == (*archPaths)[i]) {
                (*fileSizes)[i] = e.size;
                totalBytes += e.size;
                break;
            }
        }
    }

    // Heap-copy the completion block; released exactly once in the final cleanup.
    void (^hHeap)(NSError*) = [h copy];

    // ── Phase 1: UI dialogs on main thread ───────────────────────────────────
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            // Overwrite confirmation
            bool skipExisting = false;
            if (existing.count > 0) {
                QString msg;
                if (existing.count == 1)
                    msg = QObject::tr(
                              "The following file already exists at the destination:\n%1\n\nOverwrite it?")
                          .arg(QString::fromNSString(existing[0]));
                else
                    msg = QObject::tr(
                              "%1 files already exist at the destination. What would you like to do?")
                          .arg((int)existing.count);

                QMessageBox dlgBox(parentWidget);
                dlgBox.setWindowTitle(QObject::tr("Files Already Exist"));
                dlgBox.setText(msg);
                dlgBox.setIcon(QMessageBox::Question);
                dlgBox.setStandardButtons(
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
                dlgBox.setButtonText(QMessageBox::Yes, QObject::tr("Overwrite"));
                dlgBox.setButtonText(QMessageBox::No, QObject::tr("Skip Existing"));
                dlgBox.setDefaultButton(QMessageBox::Yes);
                int ret = dlgBox.exec();

                if (ret == QMessageBox::Cancel) {
                    hHeap(nil);
                    [hHeap release];
                    return;
                }
                skipExisting = (ret == QMessageBox::No);
            }

            // Progress dialog — per-mille (0–1000) for byte-accurate display
            QProgressDialog* prog = new QProgressDialog(
                QObject::tr("Extracting files..."), QObject::tr("Cancel"),
                0, 1000, parentWidget);
            prog->setWindowModality(Qt::ApplicationModal);
            prog->setMinimumDuration(0);
            prog->show();
            QApplication::processEvents();

            auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
            QObject::connect(prog, &QProgressDialog::canceled, [cancelFlag, engine]() {
                cancelFlag->store(true);
                engine->cancelExtract(); // abort the current Extract() call
            });

            // ── Phase 2: extraction on a GCD global queue ─────────────────────
            dispatch_async(
                dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                @autoreleasepool {
                    NSFileManager* threadFm = [NSFileManager defaultManager];

                    // ProgressInfo lives on this thread; QElapsedTimer is thread-safe.
                    ProgressInfo pi;
                    pi.start(totalBytes);
                    uint64_t baseBytes = 0;

                    for (int i = 0; i < fileCount; i++) {
                        if (cancelFlag->load()) break;

                        NSString* nsFullDest = [NSString stringWithUTF8String:
                            (destDirStr + "/" + (*dispPaths)[i]).c_str()];
                        QString filename = QString::fromStdString((*dispPaths)[i]);

                        // Show filename immediately, before the (potentially slow) read
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (!cancelFlag->load())
                                prog->setLabelText(filename);
                        });

                        if (skipExisting && [threadFm fileExistsAtPath:nsFullDest]) {
                            baseBytes += (*fileSizes)[i];
                            pi.bytesProcessed = baseBytes;
                            continue;
                        }

                        // Create destination directory (Extract() in ZipEngine doesn't)
                        [threadFm createDirectoryAtPath:
                            [nsFullDest stringByDeletingLastPathComponent]
                                withIntermediateDirectories:YES attributes:nil error:nil];

                        // Per-chunk streaming callback.
                        // Gated to 100 ms so we don't flood the main queue
                        // (ZipEngine fires every 256 KB; at 1 GB/s that's ~4000/s).
                        int64_t lastDispatchMs = -100;
                        uint64_t capturedBase = baseBytes;

                        engine->setExtractProgressCb(
                            [&, capturedBase](const ArchiveEngine::ExtractProgressInfo& info)
                        {
                            if (cancelFlag->load()) return;
                            int64_t now = pi.timer.elapsed();
                            if (now - lastDispatchMs < 100) return;
                            lastDispatchMs = now;

                            pi.bytesProcessed = capturedBase + info.bytesProcessed;
                            pi.updateRate();

                            int perMille = totalBytes > 0
                                ? (int)(pi.bytesProcessed * 1000 / totalBytes) : 0;
                            QString label = filename;
                            QString eta = pi.etaString();
                            if (!eta.isEmpty()) label += "\n" + eta;

                            dispatch_async(dispatch_get_main_queue(), ^{
                                if (!cancelFlag->load()) {
                                    prog->setValue(perMille);
                                    prog->setLabelText(label);
                                }
                            });
                        });

                        std::string destStr = destDirStr + "/" + (*dispPaths)[i];
                        bool extracted = engine->Extract((*archPaths)[i], destStr);
                        engine->setExtractProgressCb(nullptr);

                        // TarGzEngine (and other engines whose Extract() reads in
                        // chunks rather than one gzread call) can silently produce
                        // a 0-byte file on GCD background threads.  Fall back to
                        // ReadFile() + manual write, which is how the engine was
                        // always called before this change and is known to work.
                        // Skip the fallback if cancelled — Extract() also returns false
                        // on cancel, but we don't want to retry a cancelled operation.
                        if (!extracted && !cancelFlag->load()) {
                            auto data = engine->ReadFile((*archPaths)[i]);
                            if (!data.empty()) {
                                std::ofstream out(destStr, std::ios::binary);
                                out.write(
                                    reinterpret_cast<const char*>(data.data()),
                                    static_cast<std::streamsize>(data.size()));
                            }
                        }

                        // Final per-file update: covers formats where the callback
                        // is never called (e.g. LibarchiveEngine falls back to ReadFile),
                        // and snaps the bar to the exact completed-bytes position.
                        baseBytes += (*fileSizes)[i];
                        pi.bytesProcessed = baseBytes;
                        pi.updateRate();

                        int perMille = totalBytes > 0
                            ? (int)(pi.bytesProcessed * 1000 / totalBytes)
                            : (i + 1) * 1000 / fileCount;
                        {
                            QString label = filename;
                            QString eta = pi.etaString();
                            if (!eta.isEmpty()) label += "\n" + eta;
                            dispatch_async(dispatch_get_main_queue(), ^{
                                if (!cancelFlag->load()) {
                                    prog->setValue(perMille);
                                    prog->setLabelText(label);
                                }
                            });
                        }
                    }

                    // ── Phase 3: cleanup ──────────────────────────────────────
                    // Set the flag before deleting prog so any in-flight main-queue
                    // blocks that slipped past the FIFO ordering via processEvents()
                    // do not touch the deleted widget.
                    dispatch_async(dispatch_get_main_queue(), ^{
                        @autoreleasepool {
                            cancelFlag->store(true);
                            prog->setValue(1000);
                            delete prog;
                            hHeap(nil);
                            [hHeap release];
                        }
                    });
                }
            });
        }
    });
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)s sourceOperationMaskForDraggingContext:(NSDraggingContext)ctx
{
    return NSDragOperationCopy;
}
- (void)dealloc { [_archivePaths release]; [_displayPaths release]; [super dealloc]; }
@end

static const char kAssocKey = '\0';

extern "C" int startMacFilePromiseDrag(void* nsview, ArchiveEngine* engine, const char* files, int count, QWidget* parent)
{
    NSMutableArray* archivePaths = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray* displayPaths = [NSMutableArray arrayWithCapacity:count];
    const char* p = files;
    for (int i = 0; i < count; i++) {
        std::string ap(p); p += ap.size() + 1;
        std::string dp(p); p += dp.size() + 1;
        [archivePaths addObject:[NSString stringWithUTF8String:ap.c_str()]];
        [displayPaths addObject:[NSString stringWithUTF8String:dp.c_str()]];
    }

    BatchDelegate* d = [[BatchDelegate alloc] init];
    d.engine = engine;
    d.archivePaths = archivePaths;
    d.displayPaths = displayPaths;
    d.parentWidget = parent;

    NSFilePromiseProvider* prv = [[NSFilePromiseProvider alloc] initWithFileType:@"public.data" delegate:d];
    objc_setAssociatedObject(prv, &kAssocKey, d, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [d release];

    NSView* view = (__bridge NSView*)nsview;
    NSEvent* evt = [NSApp currentEvent];
    if (!view || !evt) { [prv release]; return -1; }

    NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:prv];
    [item setDraggingFrame:NSMakeRect(0, 0, 32, 32)
                 contents:[[[NSImage alloc] initWithSize:NSMakeSize(32, 32)] autorelease]];

    [view beginDraggingSessionWithItems:@[item] event:evt source:d];

    [prv release]; [item release];
    return 0;
}
