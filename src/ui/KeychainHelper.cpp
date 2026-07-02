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
    // Session-scoped by default: the credential disappears when the user
    // logs off, rather than persisting indefinitely on the machine.
    cred.Persist = CRED_PERSIST_SESSION;

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

static CFStringRef cfString(const QByteArray& utf8)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(utf8.constData()),
        utf8.size(), kCFStringEncodingUTF8, false);
}

static CFMutableDictionaryRef baseQuery(const QByteArray& keyData)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFStringRef service = cfString(QByteArray(kKeychainService));
    CFDictionarySetValue(query, kSecAttrService, service);
    CFRelease(service);
    CFStringRef account = cfString(keyData);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(account);
    return query;
}

bool KeychainHelper::save(const QString& key, const QString& secret)
{
    QByteArray keyData = toUTF8(key);
    QByteArray secretData = toUTF8(secret);

    CFMutableDictionaryRef query = baseQuery(keyData);
    // Remove any existing item first (SecItemAdd fails if one exists).
    SecItemDelete(query);

    CFDataRef valueData = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(secretData.constData()), secretData.size());
    CFDictionarySetValue(query, kSecValueData, valueData);

    OSStatus status = SecItemAdd(query, nullptr);

    CFRelease(valueData);
    CFRelease(query);
    return status == errSecSuccess;
}

QString KeychainHelper::load(const QString& key)
{
    QByteArray keyData = toUTF8(key);

    CFMutableDictionaryRef query = baseQuery(keyData);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);

    if (status != errSecSuccess || !result)
        return {};

    CFDataRef data = static_cast<CFDataRef>(result);
    QString value = QString::fromUtf8(
        reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
        static_cast<int>(CFDataGetLength(data)));
    CFRelease(result);
    return value;
}

bool KeychainHelper::remove(const QString& key)
{
    QByteArray keyData = toUTF8(key);
    CFMutableDictionaryRef query = baseQuery(keyData);
    OSStatus status = SecItemDelete(query);
    CFRelease(query);
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

#else // !ZIPFX_HAVE_LIBSECRET — no secure backend available on this system

// Without libsecret there is no OS-managed secret store on this Linux
// system. Rather than silently writing the password in plaintext to
// ~/.config/<org>/ZipFX.conf (readable by any process running as this
// user), refuse to persist it at all. Callers must surface this failure
// to the user instead of assuming the password was saved.
bool KeychainHelper::save(const QString&, const QString&)
{
    return false;
}

QString KeychainHelper::load(const QString&)
{
    return {};
}

bool KeychainHelper::remove(const QString&)
{
    return true;
}

#endif // ZIPFX_HAVE_LIBSECRET
#endif // platform
