#ifndef ZIPFX_LOGGING_H
#define ZIPFX_LOGGING_H

#include <QDebug>
#include <QString>
#include <cstdio>
#include <cstdarg>

// Compatibility shim: redirect wxWidgets log macros to Qt's debug output
// so engine code doesn't need rewriting.

static inline QString _qt_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return QString::fromUtf8(buf);
}

#define wxLogDebug(...)   qDebug().noquote()  << _qt_printf(__VA_ARGS__)
#define wxLogWarning(...) qWarning().noquote() << _qt_printf(__VA_ARGS__)
#define wxLogError(...)   qCritical().noquote() << _qt_printf(__VA_ARGS__)

#endif
