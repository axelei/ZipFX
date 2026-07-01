#include "KeychainHelper.h"

#include <QSettings>
#include <QStringList>

// ── Windows: Credential Manager ──────────────────────────────────────────
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

static QString targetName(const QString& key)
{
    return "ZipFX:" + key;
}

bool KeychainHelper::save(const QString& key, const QString& secret)
{
    QString target = targetName(key);
    std::wstring targetW = target.toStdWString();
    std::wstring secretW = secret.toStdWString();

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(targetW.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(secretW.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(secretW.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&cred, 0) != FALSE;
}

QString KeychainHelper::load(const QString& key)
{
    QString target = targetName(key);
    std::wstring targetW = target.toStdWString();

    PCREDENTIALW cred = nullptr;
    if (!CredReadW(targetW.c_str(), CRED_TYPE_GENERIC, 0, &cred))
        return {};

    QString result;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0)
    {
        result = QString::fromWCharArray(
            reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
            cred->CredentialBlobSize / sizeof(wchar_t));
    }

    CredFree(cred);
    return result;
}

bool KeychainHelper::remove(const QString& key)
{
    QString target = targetName(key);
    std::wstring targetW = target.toStdWString();
    return CredDeleteW(targetW.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
}

// ── macOS: Keychain (Security framework) ─────────────────────────────────
#elif defined(__APPLE__)
#include <cstring>
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>

constexpr const char* kKeychainService = "ZipFX";

static QByteArray toUTF8(const QString& s)
{
    return s.toUtf8();
}

bool KeychainHelper::save(const QString& key, const QString& secret)
{
    QByteArray keyData = toUTF8(key);
    QByteArray secretData = toUTF8(secret);

    // Remove existing first
    SecKeychainItemRef item = nullptr;
    if (SecKeychainFindGenericPassword(nullptr,
            static_cast<uint32_t>(std::strlen(kKeychainService)), kKeychainService,
            static_cast<uint32_t>(keyData.size()), keyData.constData(),
            nullptr, nullptr, &item) == errSecSuccess)
    {
        SecKeychainItemDelete(item);
        CFRelease(item);
    }

    OSStatus status = SecKeychainAddGenericPassword(nullptr,
        static_cast<uint32_t>(std::strlen(kKeychainService)), kKeychainService,
        static_cast<uint32_t>(keyData.size()), keyData.constData(),
        static_cast<uint32_t>(secretData.size()), secretData.constData(),
        nullptr);

    return status == errSecSuccess;
}

QString KeychainHelper::load(const QString& key)
{
    QByteArray keyData = toUTF8(key);

    void* passwordData = nullptr;
    uint32_t passwordLength = 0;

    OSStatus status = SecKeychainFindGenericPassword(nullptr,
        static_cast<uint32_t>(std::strlen(kKeychainService)), kKeychainService,
        static_cast<uint32_t>(keyData.size()), keyData.constData(),
        &passwordLength, &passwordData, nullptr);

    if (status != errSecSuccess)
        return {};

    QString result = QString::fromUtf8(
        static_cast<const char*>(passwordData),
        static_cast<int>(passwordLength));

    SecKeychainItemFreeContent(nullptr, passwordData);
    return result;
}

bool KeychainHelper::remove(const QString& key)
{
    QByteArray keyData = toUTF8(key);

    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr,
        static_cast<uint32_t>(std::strlen(kKeychainService)), kKeychainService,
        static_cast<uint32_t>(keyData.size()), keyData.constData(),
        nullptr, nullptr, &item);

    if (status != errSecSuccess) return false;

    status = SecKeychainItemDelete(item);
    CFRelease(item);
    return status == errSecSuccess;
}

// ── Linux: libsecret (optional) with QSettings fallback ──────────────────
#else
#ifdef ZIPFX_HAVE_LIBSECRET
#include <libsecret/secret.h>

constexpr const char* kKeychainSchema = "net.krusher.zipfx.Password";
constexpr const char* kKeychainAttr = "archive-path";

static const SecretSchema* zipfxSchema()
{
    static const SecretSchema schema = {
        kKeychainSchema, SECRET_SCHEMA_NONE,
        {
            { kKeychainAttr, SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING },
        }
    };
    return &schema;
}

bool KeychainHelper::save(const QString& key, const QString& secret)
{
    QByteArray keyData = key.toUtf8();
    QByteArray secretData = secret.toUtf8();

    GError* error = nullptr;
    bool ok = secret_password_storev_sync(
        zipfxSchema(),
        SECRET_COLLECTION_DEFAULT,
        ("ZipFX: " + key).toUtf8().constData(),
        {{kKeychainAttr, keyData.constData()}},
        nullptr,
        &error,
        secretData.constData());

    if (error)
    {
        g_error_free(error);
        return false;
    }
    return ok;
}

QString KeychainHelper::load(const QString& key)
{
    QByteArray keyData = key.toUtf8();

    GError* error = nullptr;
    gchar* password = secret_password_lookupv_sync(
        zipfxSchema(),
        nullptr,
        &error,
        kKeychainAttr, keyData.constData(),
        nullptr);

    if (error)
    {
        g_error_free(error);
        return {};
    }

    if (!password) return {};

    QString result = QString::fromUtf8(password);
    secret_password_free(password);
    return result;
}

bool KeychainHelper::remove(const QString& key)
{
    QByteArray keyData = key.toUtf8();

    GError* error = nullptr;
    bool ok = secret_password_clearv_sync(
        zipfxSchema(),
        nullptr,
        &error,
        kKeychainAttr, keyData.constData(),
        nullptr);

    if (error)
    {
        g_error_free(error);
        return false;
    }
    return ok;
}

#else // !ZIPFX_HAVE_LIBSECRET — fallback to QSettings

static const char* kSettingsGroup = "passwordManager";

bool KeychainHelper::save(const QString& key, const QString& secret)
{
    QSettings s;
    s.beginGroup(kSettingsGroup);
    QStringList keys = s.childKeys();
    for (const auto& k : keys)
        if (k == key) { s.remove(k); break; }
    s.setValue(key, secret);
    s.endGroup();
    return true;
}

QString KeychainHelper::load(const QString& key)
{
    QSettings s;
    s.beginGroup(kSettingsGroup);
    QString result = s.value(key).toString();
    s.endGroup();
    return result;
}

bool KeychainHelper::remove(const QString& key)
{
    QSettings s;
    s.beginGroup(kSettingsGroup);
    s.remove(key);
    s.endGroup();
    return true;
}

#endif // ZIPFX_HAVE_LIBSECRET
#endif // platform
