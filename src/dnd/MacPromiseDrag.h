#ifndef ZIPFX_MAC_PROMISE_DRAG_H
#define ZIPFX_MAC_PROMISE_DRAG_H

class ArchiveEngine;

#ifdef __cplusplus
extern "C" {
#endif

int startMacFilePromiseDrag(void* nsview, ArchiveEngine* engine, const char* files, int count);

#ifdef __cplusplus
}
#endif

#endif
