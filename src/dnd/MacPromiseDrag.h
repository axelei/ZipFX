#ifndef ZIPFX_MAC_PROMISE_DRAG_H
#define ZIPFX_MAC_PROMISE_DRAG_H

class ArchiveEngine;
class QWidget;

#ifdef __cplusplus
extern "C" {
#endif

int startMacFilePromiseDrag(void* nsview, ArchiveEngine* engine, const char* files, int count, QWidget* parent);

#ifdef __cplusplus
}
#endif

#endif
