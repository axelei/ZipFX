#ifndef ZIPFX_LOGGING_H
#define ZIPFX_LOGGING_H

#include <QDebug>
#include <QString>
#include <cstdio>
#include <cstdarg>

// Printf-style logging macros using Qt's debug output.
// Usage:  LOG_DBG("string %1 %2", arg1, arg2)
//         LOG_WARN("warning %1", val)
//         LOG_ERR("error %1", val)

inline QString _qt_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[4096];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return QString::fromUtf8(buf);
}

#define LOG_DBG(...)   qDebug().noquote()  << _qt_printf(__VA_ARGS__)
#define LOG_WARN(...)  qWarning().noquote() << _qt_printf(__VA_ARGS__)
#define LOG_ERR(...)   qCritical().noquote() << _qt_printf(__VA_ARGS__)

#endif
