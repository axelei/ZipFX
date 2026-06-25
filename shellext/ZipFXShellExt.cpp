#include "ZipFXShellExt.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <objbase.h>
#include <strsafe.h>

#include <string>
#include <vector>

// ── DLL globals ────────────────────────────────────────────────────────────
volatile LONG g_dllRefCount = 0;
HMODULE       g_hModule     = nullptr;

// ── Known archive extensions ───────────────────────────────────────────────
static const wchar_t* kArchiveExts[] = {
    L".zip", L".7z", L".rar", L".tar", L".gz", L".bz2", L".xz",
    L".zst", L".lz4", L".tgz", L".tbz2", L".txz", L".tzst",
    L".iso", L".cab", L".lha", L".lzh", L".arj", L".arc",
    L".wad", L".pak", L".grp", L".hog", L".vpk", L".gob",
    L".rff", L".big", L".viv", L".pod", L".mpq", L".adf",
    L".chd", L".jar", L".apk", L".epub", L".war", L".ear",
    L".docx", L".xlsx", L".pptx", L".odt", L".ods", L".odp",
    nullptr
};

// ── CLSID string ──────────────────────────────────────────────────────────
static const wchar_t* kClsidStr = L"{4DC2B9E2-0A3F-4D27-8B3A-E1F2C3D4A5B6}";

// ═══════════════════════════════════════════════════════════════════════════
// DLL entry points
// ═══════════════════════════════════════════════════════════════════════════
BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_ZipFXContextMenu))
        return CLASS_E_CLASSNOTAVAILABLE;

    auto* pFactory = new (std::nothrow) ZipFXClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return (g_dllRefCount == 0) ? S_OK : S_FALSE;
}

// ── Registry helpers ───────────────────────────────────────────────────────
static LSTATUS RegSetSZ(HKEY hRoot, const wchar_t* subKey,
                         const wchar_t* valueName, const wchar_t* data)
{
    HKEY hKey = nullptr;
    LSTATUS st = RegCreateKeyExW(hRoot, subKey, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE,
                                  KEY_WRITE, nullptr, &hKey, nullptr);
    if (st != ERROR_SUCCESS) return st;
    st = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(data),
                         static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return st;
}

STDAPI DllRegisterServer()
{
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    // CLSID\{...}
    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr;
    RegSetSZ(HKEY_CURRENT_USER, clsidKey.c_str(), nullptr, L"ZipFX Context Menu Handler");

    // CLSID\{...}\InProcServer32
    std::wstring inprocKey = clsidKey + L"\\InProcServer32";
    RegSetSZ(HKEY_CURRENT_USER, inprocKey.c_str(), nullptr, dllPath);
    RegSetSZ(HKEY_CURRENT_USER, inprocKey.c_str(), L"ThreadingModel", L"Apartment");

    // Context-menu registrations
    const wchar_t* handlers[] = {
        L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\ZipFX",
        L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\ZipFX",
        L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\ZipFX",
        nullptr
    };
    for (int i = 0; handlers[i]; ++i)
        RegSetSZ(HKEY_CURRENT_USER, handlers[i], nullptr, kClsidStr);

    // Notify Explorer
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr;

    const wchar_t* keys[] = {
        (std::wstring(L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\ZipFX")).c_str(),
        (std::wstring(L"Software\\Classes\\Directory\\shellex\\ContextMenuHandlers\\ZipFX")).c_str(),
        (std::wstring(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\ZipFX")).c_str(),
        nullptr
    };

    for (int i = 0; keys[i]; ++i)
        RegDeleteKeyW(HKEY_CURRENT_USER, keys[i]);

    RegDeleteKeyW(HKEY_CURRENT_USER, (clsidKey + L"\\InProcServer32").c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, clsidKey.c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// ═══════════════════════════════════════════════════════════════════════════
// ZipFXContextMenu
// ═══════════════════════════════════════════════════════════════════════════
ZipFXContextMenu::ZipFXContextMenu() : m_ref(1)
{
    InterlockedIncrement(&g_dllRefCount);
}

ZipFXContextMenu::~ZipFXContextMenu()
{
    InterlockedDecrement(&g_dllRefCount);
}

STDMETHODIMP_(ULONG) ZipFXContextMenu::Release()
{
    LONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) delete this;
    return ref;
}

STDMETHODIMP ZipFXContextMenu::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellExtInit))
        *ppv = static_cast<IShellExtInit*>(this);
    else if (IsEqualIID(riid, IID_IContextMenu))
        *ppv = static_cast<IContextMenu*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

// ── IShellExtInit::Initialize ──────────────────────────────────────────────
STDMETHODIMP ZipFXContextMenu::Initialize(PCIDLIST_ABSOLUTE, IDataObject* pdtobj, HKEY)
{
    m_files.clear();
    m_hasArchive    = false;
    m_singleArchive = false;

    if (!pdtobj) return E_INVALIDARG;

    FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stm = {};
    HRESULT hr = pdtobj->GetData(&fe, &stm);
    if (FAILED(hr)) return hr;

    HDROP hDrop = reinterpret_cast<HDROP>(GlobalLock(stm.hGlobal));
    UINT count  = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

    for (UINT i = 0; i < count; ++i)
    {
        wchar_t path[MAX_PATH] = {};
        DragQueryFileW(hDrop, i, path, MAX_PATH);
        m_files.emplace_back(path);
    }

    GlobalUnlock(stm.hGlobal);
    ReleaseStgMedium(&stm);

    UINT archiveCount = 0;
    for (const auto& f : m_files)
        if (isArchiveExt(PathFindExtensionW(f.c_str()))) ++archiveCount;

    m_hasArchive    = archiveCount > 0;
    m_singleArchive = (m_files.size() == 1 && archiveCount == 1);

    return S_OK;
}

// ── IContextMenu::QueryContextMenu ─────────────────────────────────────────
STDMETHODIMP ZipFXContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                                  UINT idCmdFirst, UINT /*idCmdLast*/,
                                                  UINT uFlags)
{
    if (uFlags & CMF_DEFAULTONLY)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    HMENU hSub = CreatePopupMenu();
    UINT  idx  = 0;

    // "Open with ZipFX" — always
    InsertMenuW(hSub, idx++, MF_BYPOSITION | MF_STRING,
                idCmdFirst + CMD_OPEN, L"Open with ZipFX");

    // "Add to archive..." — always
    InsertMenuW(hSub, idx++, MF_BYPOSITION | MF_STRING,
                idCmdFirst + CMD_ADD, L"Add to archive...");

    if (m_hasArchive)
    {
        InsertMenuW(hSub, idx++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        InsertMenuW(hSub, idx++, MF_BYPOSITION | MF_STRING,
                    idCmdFirst + CMD_EXTRACT_HERE, L"Extract here");

        if (m_singleArchive)
        {
            wchar_t name[MAX_PATH] = {};
            wcscpy_s(name, PathFindFileNameW(m_files[0].c_str()));
            PathRemoveExtensionW(name);
            std::wstring label = std::wstring(L"Extract to \"") + name + L"\\\"";
            InsertMenuW(hSub, idx++, MF_BYPOSITION | MF_STRING,
                        idCmdFirst + CMD_EXTRACT_TO, label.c_str());
        }
    }

    // Insert the "ZipFX" cascade into Explorer's menu
    MENUITEMINFOW mii    = {};
    mii.cbSize           = sizeof(mii);
    mii.fMask            = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
    mii.wID              = idCmdFirst;
    mii.hSubMenu         = hSub;
    mii.dwTypeData       = const_cast<LPWSTR>(L"ZipFX");
    InsertMenuItemW(hmenu, indexMenu, TRUE, &mii);

    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, CMD_COUNT);
}

// ── IContextMenu::InvokeCommand ────────────────────────────────────────────
STDMETHODIMP ZipFXContextMenu::InvokeCommand(CMINVOKECOMMANDINFO* pici)
{
    if (!IS_INTRESOURCE(pici->lpVerb))
        return E_INVALIDARG;

    UINT cmd = static_cast<UINT>(reinterpret_cast<UINT_PTR>(pici->lpVerb));

    switch (cmd)
    {
    case CMD_OPEN:
        if (!m_files.empty())
            launch(L"\"" + m_files[0] + L"\"");
        break;

    case CMD_ADD:
    {
        std::wstring args = L"--shell-add";
        for (const auto& f : m_files)
            args += L" \"" + f + L"\"";
        launch(args);
        break;
    }

    case CMD_EXTRACT_HERE:
        for (const auto& f : m_files)
        {
            if (!isArchiveExt(PathFindExtensionW(f.c_str()))) continue;
            wchar_t dir[MAX_PATH] = {};
            wcscpy_s(dir, f.c_str());
            PathRemoveFileSpecW(dir);
            launchCli(L"extract \"" + f + L"\" -o \"" + dir + L"\"");
        }
        break;

    case CMD_EXTRACT_TO:
        if (m_singleArchive)
        {
            wchar_t dir[MAX_PATH] = {};
            wcscpy_s(dir, m_files[0].c_str());
            PathRemoveFileSpecW(dir);

            wchar_t name[MAX_PATH] = {};
            wcscpy_s(name, PathFindFileNameW(m_files[0].c_str()));
            PathRemoveExtensionW(name);

            std::wstring dest = std::wstring(dir) + L"\\" + name;
            launchCli(L"extract \"" + m_files[0] + L"\" -o \"" + dest + L"\"");
        }
        break;

    default:
        return E_INVALIDARG;
    }

    return S_OK;
}

// ── IContextMenu::GetCommandString ─────────────────────────────────────────
STDMETHODIMP ZipFXContextMenu::GetCommandString(UINT_PTR idCmd, UINT uType,
                                                  UINT*, CHAR* pszName, UINT cchMax)
{
    static const wchar_t* tips[] = {
        L"Open with ZipFX",
        L"Add selected files to an archive",
        L"Extract archive contents here",
        L"Extract archive to a subfolder",
    };

    if (idCmd >= CMD_COUNT) return E_INVALIDARG;

    if (uType == GCS_HELPTEXTW)
        StringCchCopyW(reinterpret_cast<LPWSTR>(pszName), cchMax, tips[idCmd]);
    else if (uType == GCS_VALIDATEW)
        return S_OK;
    else if (uType == GCS_HELPTEXTA)
        StringCchCopyA(pszName, cchMax, "ZipFX");

    return S_OK;
}

// ── Private helpers ─────────────────────────────────────────────────────────
bool ZipFXContextMenu::isArchiveExt(const wchar_t* ext) const
{
    if (!ext || ext[0] == L'\0') return false;
    for (int i = 0; kArchiveExts[i]; ++i)
        if (_wcsicmp(ext, kArchiveExts[i]) == 0) return true;
    return false;
}

std::wstring ZipFXContextMenu::getZipFXPath() const
{
    // 1. Look next to the DLL
    wchar_t dllDir[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, dllDir, MAX_PATH);
    PathRemoveFileSpecW(dllDir);

    wchar_t candidate[MAX_PATH] = {};
    PathCombineW(candidate, dllDir, L"ZipFX.exe");
    if (PathFileExistsW(candidate)) return candidate;

    // 2. Registry install path
    wchar_t regPath[MAX_PATH] = {};
    DWORD   sz   = sizeof(regPath);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\ZipFX", L"InstallDir",
                     RRF_RT_REG_SZ, nullptr, regPath, &sz) == ERROR_SUCCESS)
    {
        PathCombineW(candidate, regPath, L"ZipFX.exe");
        if (PathFileExistsW(candidate)) return candidate;
    }

    return {};
}

void ZipFXContextMenu::launch(const std::wstring& args) const
{
    std::wstring exe = getZipFXPath();
    if (exe.empty()) return;

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb       = L"open";
    sei.lpFile       = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

void ZipFXContextMenu::launchCli(const std::wstring& args) const
{
    std::wstring exe = getZipFXPath();
    if (exe.empty()) return;

    // CLI mode: pass the subcommand directly (main.cpp detects "extract" etc.)
    std::wstring fullArgs = args;

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"open";
    sei.lpFile       = exe.c_str();
    sei.lpParameters = fullArgs.c_str();
    sei.nShow        = SW_HIDE;
    ShellExecuteExW(&sei);
}

// ═══════════════════════════════════════════════════════════════════════════
// ZipFXClassFactory
// ═══════════════════════════════════════════════════════════════════════════
STDMETHODIMP_(ULONG) ZipFXClassFactory::Release()
{
    LONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) delete this;
    return ref;
}

STDMETHODIMP ZipFXClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        *ppv = static_cast<IClassFactory*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}

STDMETHODIMP ZipFXClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
{
    *ppv = nullptr;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;

    auto* obj = new (std::nothrow) ZipFXContextMenu();
    if (!obj) return E_OUTOFMEMORY;

    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}

STDMETHODIMP ZipFXClassFactory::LockServer(BOOL fLock)
{
    if (fLock) InterlockedIncrement(&g_dllRefCount);
    else       InterlockedDecrement(&g_dllRefCount);
    return S_OK;
}
