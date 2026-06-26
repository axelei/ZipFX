#ifndef ZIPFX_VIRTUAL_FILE_DATA_OBJECT_H
#define ZIPFX_VIRTUAL_FILE_DATA_OBJECT_H

#include <cstdint>
#include <QString>
#include <QObject>

class DragProgressDialog;

#ifdef _WIN32
#include <ole2.h>
#include <vector>
#include <string>

class ArchiveEngine;

struct VirtualFileEntry
{
    std::wstring name;       // Relative path (e.g. L"subdir/file.txt")
    uint64_t      size;
    ArchiveEngine* engine;   // Source engine (must outlive the drag)
    std::string   archivePath; // Entry path inside the archive
};

// Minimal IDataObject that provides file contents via CFSTR_FILECONTENTS.
// Files are extracted on demand when the drop occurs — no pre-extraction.
class VirtualFileDataObject : public IDataObject
{
public:
    VirtualFileDataObject();
    ~VirtualFileDataObject();

    void AddFile(const VirtualFileEntry& entry);
    size_t GetCount() const { return m_files.size(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDataObject
    STDMETHODIMP GetData(FORMATETC* pFE, STGMEDIUM* pSTM) override;
    STDMETHODIMP GetDataHere(FORMATETC* pFE, STGMEDIUM* pSTM) override;
    STDMETHODIMP QueryGetData(FORMATETC* pFE) override;
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* pFE, FORMATETC* pOut) override;
    STDMETHODIMP SetData(FORMATETC* pFE, STGMEDIUM* pSTM, BOOL fRelease) override;
    STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppEnum) override;
    STDMETHODIMP DAdvise(FORMATETC* pFE, DWORD flags, IAdviseSink* pSink, DWORD* pConn) override;
    STDMETHODIMP DUnadvise(DWORD conn) override;
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** ppEnum) override;

private:
    volatile LONG m_refCount = 1;

    struct FileEntry
    {
        VirtualFileEntry info;
    };
    std::vector<FileEntry> m_files;

    CLIPFORMAT m_cfDescriptor = 0;
    CLIPFORMAT m_cfContents   = 0;
    int      m_progressTotal = 0;
    uint64_t m_baseBytes     = 0;   // bytes completed by previous files
    DragProgressDialog* m_progressDlg = nullptr;
    bool m_cancelled = false;

public:
    HWND m_parentHwnd = nullptr;

private:
    HRESULT GetFileDescriptor(FORMATETC* pFE, STGMEDIUM* pSTM);
    HRESULT GetFileContents(FORMATETC* pFE, STGMEDIUM* pSTM);
};

// Minimal IDropSource (required by DoDragDrop API)
class VirtualDropSource : public IDropSource
{
public:
    VirtualDropSource() = default;
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override;
    STDMETHODIMP GiveFeedback(DWORD dwEffect) override;
private:
    volatile LONG m_ref = 1;
};

// Helper: start a drag with VirtualFileDataObject (bypasses wxDropSource)
bool StartVirtualDrag(VirtualFileDataObject* data, HWND hwnd);

#endif // _WIN32
#endif
