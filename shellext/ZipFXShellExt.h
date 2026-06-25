#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

// {4DC2B9E2-0A3F-4D27-8B3A-E1F2C3D4A5B6}
static const CLSID CLSID_ZipFXContextMenu = {
    0x4DC2B9E2, 0x0A3F, 0x4D27,
    {0x8B, 0x3A, 0xE1, 0xF2, 0xC3, 0xD4, 0xA5, 0xB6}
};

extern volatile LONG g_dllRefCount;
extern HMODULE       g_hModule;

// ── Context menu handler ───────────────────────────────────────────────────
class ZipFXContextMenu final : public IShellExtInit, public IContextMenu
{
public:
    ZipFXContextMenu();
    ~ZipFXContextMenu();

    // IUnknown
    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef()  override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override;

    // IShellExtInit
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj,
                             HKEY hkeyProgID) override;

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                   UINT idCmdFirst, UINT idCmdLast,
                                   UINT uFlags) override;
    STDMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType,
                                   UINT* pReserved, CHAR* pszName,
                                   UINT cchMax) override;

private:
    enum Cmd : UINT {
        CMD_OPEN         = 0,
        CMD_ADD          = 1,
        CMD_EXTRACT_HERE = 2,
        CMD_EXTRACT_TO   = 3,
        CMD_COUNT        = 4
    };

    volatile LONG             m_ref;
    std::vector<std::wstring> m_files;
    bool                      m_hasArchive    = false;
    bool                      m_singleArchive = false;

    std::wstring getZipFXPath() const;
    bool         isArchiveExt(const wchar_t* ext) const;
    void         launch(const std::wstring& args) const;
    void         launchCli(const std::wstring& args) const;
};

// ── Class factory ──────────────────────────────────────────────────────────
class ZipFXClassFactory final : public IClassFactory
{
public:
    ZipFXClassFactory() : m_ref(1) {}

    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef()  override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    volatile LONG m_ref;
};
