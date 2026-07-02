#include "RarEngine.h"
#include "Bit7zEngine.h"
#include "LibarchiveEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <archive.h>
#include <filesystem>
#include <mutex>

extern "C" {
    int archive_read_support_format_rar(struct archive*);
    int archive_read_support_format_rar5(struct archive*);
}

namespace {
    namespace fs = std::filesystem;
    const std::vector<ArchiveEntry> kEmptyEntries;
    std::string g_rarExePath;
    bool        g_rarExeSearched = false;
    std::mutex  g_rarFindMutex;

    // A PATH search (`which`/`where`) can be hijacked by placing a malicious
    // binary earlier in PATH than the real install. Only trust the result if
    // it resolves to a well-known system binary directory.
    bool isTrustedBinDir(const std::string& exePath)
    {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(fs::path(exePath), ec);
        if (ec) return false;
        std::string dir = canonical.parent_path().string();
#ifdef _WIN32
        static const char* kTrustedDirs[] = {
            "C:\\Program Files\\WinRAR", "C:\\Program Files (x86)\\WinRAR",
        };
#elif defined(__APPLE__)
        static const char* kTrustedDirs[] = {
            "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin",
        };
#else
        static const char* kTrustedDirs[] = {
            "/usr/bin", "/usr/local/bin", "/bin", "/snap/bin",
        };
#endif
        for (const char* trusted : kTrustedDirs)
        {
            std::error_code ec2;
            if (fs::equivalent(dir, trusted, ec2) && !ec2) return true;
#ifdef _WIN32
            if (_stricmp(dir.c_str(), trusted) == 0) return true;
#else
            if (dir == trusted) return true;
#endif
        }
        return false;
    }
}

// ── Constructor / Destructor ───────────────────────────────────────────────

RarEngine::RarEngine()  = default;
RarEngine::~RarEngine() = default;

// ── Static: locate rar.exe / rar ──────────────────────────────────────────

void RarEngine::resetFindCache()
{
    std::lock_guard<std::mutex> lock(g_rarFindMutex);
    g_rarExeSearched = false;
    g_rarExePath.clear();
}

std::string RarEngine::findRarExe()
{
    std::lock_guard<std::mutex> lock(g_rarFindMutex);
    if (g_rarExeSearched) return g_rarExePath;
    g_rarExeSearched = true;

#ifdef _WIN32
    // Scoop (CLI-only, user-space install) — check before system-wide WinRAR
    {
        auto checkScoop = [&](const QString& base) -> bool {
            QString p = QDir::toNativeSeparators(base + "/scoop/apps/rar/current/rar.exe");
            if (QFile::exists(p)) { g_rarExePath = p.toStdString(); return true; }
            return false;
        };
        if (checkScoop(QDir::homePath())) return g_rarExePath;
        QByteArray scoopEnv = qgetenv("SCOOP");
        if (!scoopEnv.isEmpty())
        {
            QString p = QDir::toNativeSeparators(
                QString::fromLocal8Bit(scoopEnv) + "/apps/rar/current/rar.exe");
            if (QFile::exists(p)) { g_rarExePath = p.toStdString(); return g_rarExePath; }
        }
    }
    // Well-known WinRAR install paths (64-bit and 32-bit)
    static const char* kPaths[] = {
        "C:\\Program Files\\WinRAR\\rar.exe",
        "C:\\Program Files\\WinRAR\\Rar.exe",
        "C:\\Program Files (x86)\\WinRAR\\rar.exe",
        "C:\\Program Files (x86)\\WinRAR\\Rar.exe",
    };
    for (const char* p : kPaths)
    {
        if (QFile::exists(QString::fromUtf8(p)))
        {
            g_rarExePath = p;
            return g_rarExePath;
        }
    }
    // Registry: HKLM\SOFTWARE\WinRAR
    {
        QSettings reg(R"(HKEY_LOCAL_MACHINE\SOFTWARE\WinRAR)",
                      QSettings::NativeFormat);
        QString exe = reg.value("exe64").toString();
        if (exe.isEmpty())
            exe = reg.value("exe32").toString();
        if (!exe.isEmpty() && QFile::exists(exe))
        {
            g_rarExePath = exe.toStdString();
            return g_rarExePath;
        }
    }
    // Fallback: look in PATH via where.exe
    {
        QProcess proc;
        proc.start("where", {"rar.exe"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0)
        {
            QString line = QString::fromLocal8Bit(
                proc.readAllStandardOutput()).split('\n').first().trimmed();
            std::string candidate = line.toStdString();
            if (!line.isEmpty() && QFile::exists(line) && isTrustedBinDir(candidate))
            {
                g_rarExePath = candidate;
                return g_rarExePath;
            }
        }
    }
#elif defined(__APPLE__)
    // Check Homebrew install locations before falling back to which
    static const char* kMacPaths[] = {
        "/opt/homebrew/bin/rar",   // Apple Silicon
        "/usr/local/bin/rar",      // Intel
    };
    for (const char* p : kMacPaths)
    {
        if (QFile::exists(QString::fromUtf8(p)))
        {
            g_rarExePath = p;
            return g_rarExePath;
        }
    }
    // General PATH search
    {
        QProcess proc;
        proc.start("which", {"rar"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0)
        {
            std::string candidate = QString::fromLocal8Bit(proc.readAllStandardOutput())
                               .trimmed().toStdString();
            if (isTrustedBinDir(candidate))
                g_rarExePath = candidate;
        }
    }
#else
    // Linux: PATH search
    {
        QProcess proc;
        proc.start("which", {"rar"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0)
        {
            std::string candidate = QString::fromLocal8Bit(proc.readAllStandardOutput())
                               .trimmed().toStdString();
            if (isTrustedBinDir(candidate))
                g_rarExePath = candidate;
        }
    }
#endif

    return g_rarExePath;
}

// ── Static: package manager auto-install ──────────────────────────────────

namespace {

#if !defined(_WIN32) && !defined(__APPLE__)
// Ordered by preference; first binary found wins.
struct LinuxPM {
    const char* binary;
    const char* installArgs[4]; // null-terminated list of args after the binary
};

static const LinuxPM kLinuxPMs[] = {
    { "/usr/bin/apt-get", { "install", "-y",          "rar", nullptr } },
    { "/usr/bin/apt",     { "install", "-y",          "rar", nullptr } },
    { "/usr/bin/dnf",     { "install", "-y",          "rar", nullptr } },
    { "/usr/bin/yum",     { "install", "-y",          "rar", nullptr } },
    { "/usr/bin/zypper",  { "install", "--non-interactive", "rar", nullptr } },
    { "/usr/bin/pacman",  { "-S",      "--noconfirm", "rar", nullptr } },
    { "/usr/bin/apk",     { "add",     "rar",         nullptr, nullptr } },
    { "/sbin/apk",        { "add",     "rar",         nullptr, nullptr } },
};

const LinuxPM* detectLinuxPM()
{
    for (const auto& pm : kLinuxPMs)
        if (QFile::exists(QString::fromUtf8(pm.binary)))
            return &pm;
    return nullptr;
}
#endif

} // namespace

namespace {
#ifdef _WIN32
bool scoopAvailable()
{
    // Scoop puts its shims folder in %USERPROFILE%\scoop\shims (or %SCOOP%\shims)
    QString home = QDir::homePath();
    if (QDir(home + "/scoop/shims").exists()) return true;
    QByteArray env = qgetenv("SCOOP");
    if (!env.isEmpty() && QDir(QString::fromLocal8Bit(env) + "/shims").exists())
        return true;
    return false;
}
bool wingetAvailable()
{
    QProcess p;
    p.start("winget", {"--version"});
    return p.waitForFinished(4000) && p.exitCode() == 0;
}
#endif

// Runs rar.exe/rar with an optional password piped over stdin instead of on
// the command line, where it would otherwise be readable by any other local
// process via /proc/<pid>/cmdline (Linux) or `ps -Eww` (macOS).
bool runRarProcess(const QString& rarExe, QStringList args, const std::string& password,
                   int timeoutMs, int& exitCode, const QString& workDir = {})
{
    if (!password.empty())
        args << "-p";

    QProcess proc;
    if (!workDir.isEmpty())
        proc.setWorkingDirectory(workDir);
    proc.start(rarExe, args);
    if (!proc.waitForStarted(10000)) { exitCode = -1; return false; }

    if (!password.empty())
    {
        proc.write(QString::fromStdString(password).toUtf8());
        proc.write("\n");
        proc.closeWriteChannel();
    }

    if (!proc.waitForFinished(timeoutMs)) { proc.kill(); exitCode = -1; return false; }
    exitCode = proc.exitCode();
    return true;
}

} // namespace

bool RarEngine::canAutoInstall()
{
    static int cached = -1;
    if (cached >= 0) return cached == 1;

#ifdef _WIN32
    cached = (scoopAvailable() || wingetAvailable()) ? 1 : 0;
#elif defined(__APPLE__)
    cached = (QFile::exists("/opt/homebrew/bin/brew") ||
              QFile::exists("/usr/local/bin/brew")) ? 1 : 0;
#else
    // Need pkexec for privilege escalation and a known package manager
    cached = (QFile::exists("/usr/bin/pkexec") && detectLinuxPM()) ? 1 : 0;
#endif
    return cached == 1;
}

std::string RarEngine::autoInstallDescription()
{
#ifdef _WIN32
    return scoopAvailable() ? "scoop install rar" : "winget install RARLab.WinRAR";
#elif defined(__APPLE__)
    return "brew install rar";
#else
    const LinuxPM* pm = detectLinuxPM();
    if (!pm) return {};
    // Build display string: "pkexec apt-get install -y rar"
    std::string desc = "pkexec ";
    desc += pm->binary;
    for (int i = 0; pm->installArgs[i]; ++i)
    {
        desc += ' ';
        desc += pm->installArgs[i];
    }
    return desc;
#endif
}

std::vector<std::string> RarEngine::autoInstallArgs()
{
#ifdef _WIN32
    if (scoopAvailable())
    {
        // Scoop installs CLI-only rar.exe with no elevation required.
        // Run via PowerShell since scoop itself is a PS script.
        return {"powershell.exe", "-NoProfile", "-NonInteractive",
                "-Command", "scoop install rar"};
    }
    // Fallback: full WinRAR (includes GUI + rar.exe)
    return {"winget", "install", "RARLab.WinRAR",
            "--accept-source-agreements", "--accept-package-agreements"};
#elif defined(__APPLE__)
    // Use full path — GUI apps on macOS don't inherit the shell PATH.
    // Chain xattr to strip Gatekeeper quarantine from the installed rar binary,
    // otherwise macOS blocks execution of the freshly-downloaded binary.
    const char* brew = QFile::exists("/opt/homebrew/bin/brew")
                       ? "/opt/homebrew/bin/brew"
                       : "/usr/local/bin/brew";
    // brew exit code is preserved via &&
    std::string cmd = std::string(brew) + " install rar && { "
        "xattr -d com.apple.quarantine /opt/homebrew/bin/rar 2>/dev/null || true; "
        "xattr -d com.apple.quarantine /usr/local/bin/rar 2>/dev/null || true; }";
    return {"/bin/bash", "-c", cmd};
#else
    const LinuxPM* pm = detectLinuxPM();
    if (!pm) return {};
    std::vector<std::string> args = {"/usr/bin/pkexec", pm->binary};
    for (int i = 0; pm->installArgs[i]; ++i)
        args.push_back(pm->installArgs[i]);
    return args;
#endif
}

// ── Private helpers ────────────────────────────────────────────────────────

void RarEngine::initReader(std::string_view path)
{
    m_reader.reset();
    m_fallbackReader.reset();

    auto lib = std::make_unique<LibarchiveEngine>(
        std::vector<LibarchiveEngine::FormatRegistrar>{
            archive_read_support_format_rar,
            archive_read_support_format_rar5 },
        "RAR");
    if (!m_password.empty())
        lib->setPassword(m_password);
    bool libOk = lib->Open(path);

    auto bit7z = std::make_unique<Bit7zEngine>();
    if (bit7z->isLibraryLoaded())
    {
        bit7z->setReadOnly(true);
        if (!m_password.empty())
            bit7z->setPassword(m_password);
        if (bit7z->Open(path))
        {
            m_reader = std::move(bit7z);
            if (libOk)
                m_fallbackReader = std::move(lib);
            return;
        }
    }

    if (libOk)
        m_reader = std::move(lib);
}

int RarEngine::rarCompressionLevel() const
{
    // Map 0-9 → 0-5  (WinRAR compression levels)
    if (m_compressionLevel <= 0) return 0;                            // store
    if (m_compressionLevel >= 9) return 5;                            // best
    return std::clamp((m_compressionLevel * 5 + 4) / 9, 1, 4);
}

// ── ArchiveEngine interface ────────────────────────────────────────────────

void RarEngine::setPassword(std::string_view pwd)
{
    m_password = std::string(pwd);
    // MainWindow calls Open() first, then applies a saved/typed password
    // afterward — initReader() already ran by then, so the password must
    // also be forwarded to the already-constructed sub-reader(s) here, or
    // it would silently never reach the engine that actually decrypts
    // entry data.
    if (m_reader) m_reader->setPassword(m_password);
    if (m_fallbackReader) m_fallbackReader->setPassword(m_password);
}

bool RarEngine::Open(std::string_view path)
{
    Close();
    m_path = std::string(path);
    initReader(path);
    m_isOpen = (m_reader != nullptr);
    return m_isOpen;
}

bool RarEngine::Create(std::string_view path)
{
    Close();
    m_path = std::string(path);
    // Remove any existing file so Save() creates a fresh archive
    QFile f(QString::fromStdString(m_path));
    if (f.exists()) f.remove();
    m_isOpen = true;
    return true;
}

void RarEngine::Close()
{
    m_reader.reset();
    m_fallbackReader.reset();
    m_pending.clear();
    m_path.clear();
    m_isOpen = false;
}

const std::vector<ArchiveEntry>& RarEngine::ListContents()
{
    if (m_reader) return m_reader->ListContents();
    return kEmptyEntries;
}

std::vector<uint8_t> RarEngine::ReadFile(std::string_view entryName)
{
    if (!m_reader) return {};
    auto data = m_reader->ReadFile(entryName);
    if (data.empty() && m_fallbackReader)
        data = m_fallbackReader->ReadFile(entryName);
    return data;
}

std::vector<uint8_t> RarEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_reader) return {};
    auto data = m_reader->ReadFilePartial(entryName, maxBytes);
    if (data.empty() && m_fallbackReader)
        data = m_fallbackReader->ReadFilePartial(entryName, maxBytes);
    return data;
}

bool RarEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_reader) return false;
    if (!isSafeEntryName(std::string(entryName))) return false;
    if (m_reader->Extract(entryName, destPath))
        return true;
    if (m_fallbackReader)
        return m_fallbackReader->Extract(entryName, destPath);
    return false;
}

bool RarEngine::ExtractAll(std::string_view destPath)
{
    if (!m_reader) return false;
    // RarEngine delegates to m_reader; check is applied in the engine's own ExtractAll
    if (m_reader->ExtractAll(destPath))
        return true;
    if (m_fallbackReader)
        return m_fallbackReader->ExtractAll(destPath);
    return false;
}

bool RarEngine::AddFile(std::string_view srcPath, std::string_view archivePath)
{
    if (!isAvailable()) return false;
    m_pending.push_back({ std::string(srcPath), std::string(archivePath) });
    return true;
}

bool RarEngine::RemoveEntry(std::string_view entryName)
{
    if (!isAvailable() || m_path.empty()) return false;

    QString rarExe = QString::fromStdString(findRarExe());
    QStringList args;
    args << "d" << "-idq"
         << QDir::toNativeSeparators(QString::fromStdString(m_path))
         << QString::fromStdString(std::string(entryName));

    int exitCode = -1;
    if (!runRarProcess(rarExe, args, m_password, 60000, exitCode)) return false;

    if (exitCode == 0)
    {
        initReader(m_path);
        return true;
    }
    return false;
}

bool RarEngine::Save()
{
    if (!isAvailable() || m_path.empty()) return false;
    if (m_pending.empty()) return true;

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return false;

    // Stage pending files: tmpdir/<entryPath>
    for (const auto& pf : m_pending)
    {
        QString rel = QString::fromStdString(pf.entryPath);
        rel.replace('\\', '/');
        QString dest = tmpDir.filePath(rel);
        QDir().mkpath(QFileInfo(dest).dir().absolutePath());
        if (!QFile::copy(QString::fromStdString(pf.srcPath), dest))
            return false;
    }

    QString rarExe = QString::fromStdString(findRarExe());
    QStringList args;
    args << "a" << "-r"
         << QString("-m%1").arg(rarCompressionLevel());
    args << "-idq"
         << QDir::toNativeSeparators(QString::fromStdString(m_path))
         << ".";

    int exitCode = -1;
    if (!runRarProcess(rarExe, args, m_password, 300000, exitCode, tmpDir.path())) return false;

    bool ok = (exitCode == 0);
    if (ok)
    {
        m_pending.clear();
        initReader(m_path);
        m_isOpen = true;
    }
    return ok;
}

bool RarEngine::TestIntegrity(
    std::function<void(int, int)> progressCallback,
    std::function<bool()>         cancelFlag)
{
    if (isAvailable() && !m_path.empty())
    {
        QString rarExe = QString::fromStdString(findRarExe());
        QStringList args;
        args << "t" << "-idq"
             << QDir::toNativeSeparators(QString::fromStdString(m_path));

        int exitCode = -1;
        if (!runRarProcess(rarExe, args, m_password, 120000, exitCode)) return false;
        return exitCode == 0;
    }

    if (m_reader)
        return m_reader->TestIntegrity(progressCallback, cancelFlag);

    return false;
}

bool RarEngine::SupportsViewFile() const
{
    if (m_reader) return m_reader->SupportsViewFile();
    return false;
}

std::string RarEngine::ViewUnsupportedReason() const
{
    if (m_reader) return m_reader->ViewUnsupportedReason();
    return "No reader available for this RAR archive";
}
