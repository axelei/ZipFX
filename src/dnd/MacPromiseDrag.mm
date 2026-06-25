#include <Cocoa/Cocoa.h>
#include <objc/runtime.h>
#include "engine/ArchiveEngine.h"

@interface BatchDelegate : NSObject <NSFilePromiseProviderDelegate, NSDraggingSource>
@property (readwrite) ArchiveEngine* engine;
@property (copy) NSArray* archivePaths;
@property (copy) NSArray* displayPaths;
@end

@implementation BatchDelegate
- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)p fileNameForType:(NSString*)t
{
    // Use a fixed filename — the actual files are extracted in writePromiseToURL:
    return @"ZipFX_Export.extracted";
}
- (void)filePromiseProvider:(NSFilePromiseProvider*)p writePromiseToURL:(NSURL*)url completionHandler:(void (^)(NSError*))h
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSError* err = nil;
        @try {
            // The URL's parent directory is the drop destination.
            // Extract ALL files with subdirectory structure there.
            NSString* destDir = [[url path] stringByDeletingLastPathComponent];
            NSFileManager* fm = [NSFileManager defaultManager];
            for (NSUInteger i = 0; i < self.archivePaths.count; i++) {
                NSString* fullDest = [destDir stringByAppendingPathComponent:self.displayPaths[i]];
                NSString* dir = [fullDest stringByDeletingLastPathComponent];
                [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
                std::string ep([self.archivePaths[i] UTF8String]);
                auto data = self.engine->ReadFile(ep);
                if (!data.empty()) {
                    NSData* nd = [NSData dataWithBytes:data.data() length:data.size()];
                    [nd writeToFile:fullDest atomically:NO];
                }
            }
        } @catch (NSException* e) {
            err = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteUnknownError userInfo:@{NSLocalizedDescriptionKey: [e reason]}];
        }
        h(err);
    });
}
- (NSDragOperation)draggingSession:(NSDraggingSession*)s sourceOperationMaskForDraggingContext:(NSDraggingContext)ctx
{
    return NSDragOperationCopy;
}
- (void)dealloc { [_archivePaths release]; [_displayPaths release]; [super dealloc]; }
@end

static const char kAssocKey = '\0';

extern "C" int startMacFilePromiseDrag(void* nsview, ArchiveEngine* engine, const char* files, int count)
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
