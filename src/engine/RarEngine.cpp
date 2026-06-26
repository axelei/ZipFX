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

extern "C" {
    int archive_read_support_format_rar(struct archive*);
    int archive_read_support_format_rar5(struct archive*);
}

namespace {
    const std::vector<ArchiveEntry> kEmptyEntries;
    std::string g_rarExePath;
    bool        g_rarExeSearched = false;
}

// ── Constructor / Destructor ───────────────────────────────────────────────

RarEngine::RarEngine()  = default;
RarEngine::~RarEngine() = default;

// ── Static: locate rar.exe / rar ──────────────────────────────────────────

void RarEngine::resetFindCache()
{
    g_rarExeSearched = false;
    g_rarExePath.clear();
}

std::string RarEngine::findRarExe()
{
    if (g_rarExeSearched) return g_rarExePath;
    g_rarExeSearched = true;

#ifdef _WIN32
    // Well-known install paths (64-bit and 32-bit WinRAR)
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
            if (!line.isEmpty() && QFile::exists(line))
            {
                g_rarExePath = line.toStdString();
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
            g_rarExePath = QString::fromLocal8Bit(proc.readAllStandardOutput())
                               .trimmed().toStdString();
        }
    }
#else
    // Linux: PATH search
    {
        QProcess proc;
        proc.start("which", {"rar"});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0)
        {
            g_rarExePath = QString::fromLocal8Bit(proc.readAllStandardOutput())
                               .trimmed().toStdString();
        }
    }
#endif

    return g_rarExePath;
}

// ── Static: package manager auto-install ──────────────────────────────────

bool RarEngine::canAutoInstall()
{
    static int cached = -1;
    if (cached >= 0) return cached == 1;

#ifdef _WIN32
    QProcess p;
    p.start("winget", {"--version"});
    cached = (p.waitForFinished(4000) && p.exitCode() == 0) ? 1 : 0;
#elif defined(__APPLE__)
    cached = (QFile::exists("/opt/homebrew/bin/brew") ||
              QFile::exists("/usr/local/bin/brew")) ? 1 : 0;
#else
    cached = 0;
#endif
    return cached == 1;
}

std::string RarEngine::autoInstallDescription()
{
#ifdef _WIN32
    return "winget install RARLab.WinRAR";
#elif defined(__APPLE__)
    return "brew install rar";
#else
    return {};
#endif
}

std::vector<std::string> RarEngine::autoInstallArgs()
{
#ifdef _WIN32
    return {"winget", "install", "RARLab.WinRAR",
            "--accept-source-agreements", "--accept-package-agreements"};
#elif defined(__APPLE__)
    // Use the full path so the brew binary is found even inside a .app bundle
    const char* brew = QFile::exists("/opt/homebrew/bin/brew")
                       ? "/opt/homebrew/bin/brew"
                       : "/usr/local/bin/brew";
    return {brew, "install", "rar"};
#else
    return {};
#endif
}

// ── Private helpers ────────────────────────────────────────────────────────

void RarEngine::initReader(std::string_view path)
{
    m_reader.reset();

    auto bit7z = std::make_unique<Bit7zEngine>();
    if (bit7z->isLibraryLoaded())
    {
        bit7z->setReadOnly(true);
        if (!m_password.empty())
            bit7z->setPassword(m_password);
        if (bit7z->Open(path))
        {
            m_reader = std::move(bit7z);
            return;
        }
    }

    auto lib = std::make_unique<LibarchiveEngine>(
        std::vector<LibarchiveEngine::FormatRegistrar>{
            archive_read_support_format_rar,
            archive_read_support_format_rar5 },
        "RAR");
    if (!m_password.empty())
        lib->setPassword(m_password);
    if (lib->Open(path))
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
    m_pending.clear();
    m_path.clear();
    m_isOpen = false;
}

const std::vector<ArchiveEntry>& RarEngine::ListContents()
{
    if (m_reader) return m_reader->ListContents();
    return kEmptyEntries;
}

bool RarEngine::Extract(std::string_view entryName, std::string_view destPath)
{
    if (!m_reader) return false;
    return m_reader->Extract(entryName, destPath);
}

bool RarEngine::ExtractAll(std::string_view destPath)
{
    if (!m_reader) return false;
    return m_reader->ExtractAll(destPath);
}

std::vector<uint8_t> RarEngine::ReadFile(std::string_view entryName)
{
    if (!m_reader) return {};
    return m_reader->ReadFile(entryName);
}

std::vector<uint8_t> RarEngine::ReadFilePartial(std::string_view entryName, size_t maxBytes)
{
    if (!m_reader) return {};
    return m_reader->ReadFilePartial(entryName, maxBytes);
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

    QProcess proc;
    proc.start(rarExe, args);
    if (!proc.waitForFinished(60000)) { proc.kill(); return false; }

    if (proc.exitCode() == 0)
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
    if (!m_password.empty())
        args << QString("-p%1").arg(QString::fromStdString(m_password));
    args << "-idq"
         << QDir::toNativeSeparators(QString::fromStdString(m_path))
         << ".";

    QProcess proc;
    proc.setWorkingDirectory(tmpDir.path());
    proc.start(rarExe, args);
    if (!proc.waitForFinished(300000)) { proc.kill(); return false; }

    bool ok = (proc.exitCode() == 0);
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

        QProcess proc;
        proc.start(rarExe, args);
        if (!proc.waitForFinished(120000)) { proc.kill(); return false; }
        return (proc.exitCode() == 0);
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
