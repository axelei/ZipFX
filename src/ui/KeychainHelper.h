#ifndef ZIPFX_KEYCHAIN_HELPER_H
#define ZIPFX_KEYCHAIN_HELPER_H

#include <QString>

class KeychainHelper
{
public:
    static bool save(const QString& key, const QString& secret);
    static QString load(const QString& key);
    static bool remove(const QString& key);
};

#endif
