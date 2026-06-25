#ifdef _WIN32

#include "VirtualFileDataObject.h"

#include <QDebug>
#include <QApplication>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <thread>
#include "DragProgressDialog.h"
#include <shlobj.h>

namespace fs = std::filesystem;

#include "engine/ArchiveEngine.h"
#include "engine/Logging.h"

// Registered clipboard formats
static CLIPFORMAT GetFileDescriptorFormat()
{
    static CLIPFORMAT cf = ::RegisterClipboardFormatW(L"FileGroupDescriptorW");
    return cf;
}

static CLIPFORMAT GetFileContentsFormat()
{
    static CLIPFORMAT cf = ::RegisterClipboardFormatW(L"FileContents");
    return cf;
}

// IStream that reads from a temp file (no large memory buffer).
// The temp file is deleted when the stream is released.
class FileStream : public IStream
{
    HANDLE m_hFile;
    std::wstring m_path;
    volatile LONG m_ref = 1;

public:
    FileStream(const std::wstring& path, HANDLE hFile)
        : m_path(path), m_hFile(hFile) {}

    ~FileStream()
    {
        if (m_hFile) CloseHandle(m_hFile);
        DeleteFileW(m_path.c_str());
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IStream)
        {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return r;
    }

    STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override
    {
        DWORD read = 0;
        if (!ReadFile(m_hFile, pv, cb, &read, nullptr))
            return E_FAIL;
        if (pcbRead) *pcbRead = read;
        return read > 0 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override
    {
        LARGE_INTEGER pos = {};
        DWORD method = (origin == STREAM_SEEK_CUR) ? FILE_CURRENT
                    : (origin == STREAM_SEEK_END) ? FILE_END : FILE_BEGIN;
        if (!SetFilePointerEx(m_hFile, move, &pos, method))
            return STG_E_INVALIDFUNCTION;
        if (newPos) *newPos = *reinterpret_cast<ULARGE_INTEGER*>(&pos);
        return S_OK;
    }
    STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return STG_E_REVERTED; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP Stat(STATSTG* stat, DWORD) override
    {
        memset(stat, 0, sizeof(*stat));
        LARGE_INTEGER size = {};
        if (GetFileSizeEx(m_hFile, &size) && size.QuadPart >= 0)
            stat->cbSize.QuadPart = size.QuadPart;
        stat->type = STGTY_STREAM;
        stat->grfMode = STGM_READ;
        return S_OK;
    }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }
};

// ── VirtualFileDataObject ─────────────────────────────────────────────

VirtualFileDataObject::VirtualFileDataObject()
{
    m_cfDescriptor = GetFileDescriptorFormat();
    m_cfContents   = GetFileContentsFormat();
}

VirtualFileDataObject::~VirtualFileDataObject()
{
    if (m_progressDlg)
    {
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
    }
}

void VirtualFileDataObject::AddFile(const VirtualFileEntry& entry)
{
    m_files.push_back({entry});
    m_progressTotal = static_cast<int>(m_files.size());
}

// IUnknown
STDMETHODIMP VirtualFileDataObject::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IDataObject)
    {
        *ppv = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) VirtualFileDataObject::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) VirtualFileDataObject::Release()
{
    LONG r = InterlockedDecrement(&m_refCount);
    if (r == 0) delete this;
    return r;
}

// IDataObject
STDMETHODIMP VirtualFileDataObject::GetData(FORMATETC* pFE, STGMEDIUM* pSTM)
{
    if (!pFE || !pSTM) return E_INVALIDARG;
    memset(pSTM, 0, sizeof(*pSTM));

    if (pFE->cfFormat == m_cfDescriptor)
        return GetFileDescriptor(pFE, pSTM);
    if (pFE->cfFormat == m_cfContents)
        return GetFileContents(pFE, pSTM);

    return DATA_E_FORMATETC;
}

STDMETHODIMP VirtualFileDataObject::GetDataHere(FORMATETC*, STGMEDIUM*)
{
    return E_NOTIMPL;
}

STDMETHODIMP VirtualFileDataObject::QueryGetData(FORMATETC* pFE)
{
    if (!pFE) return E_INVALIDARG;
    if (pFE->cfFormat == m_cfDescriptor && (pFE->tymed & TYMED_HGLOBAL))
        return S_OK;
    if (pFE->cfFormat == m_cfContents && (pFE->tymed & TYMED_ISTREAM))
        return S_OK;
    return DV_E_TYMED;
}

STDMETHODIMP VirtualFileDataObject::GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut)
{
    if (pOut) memset(pOut, 0, sizeof(*pOut));
    return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP VirtualFileDataObject::SetData(FORMATETC*, STGMEDIUM*, BOOL)
{
    return E_NOTIMPL;
}

STDMETHODIMP VirtualFileDataObject::EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppEnum)
{
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = nullptr;

    if (dir != DATADIR_GET) return E_NOTIMPL;

    FORMATETC fmts[2];
    fmts[0].cfFormat = m_cfDescriptor;
    fmts[0].ptd      = nullptr;
    fmts[0].dwAspect = DVASPECT_CONTENT;
    fmts[0].lindex   = -1;
    fmts[0].tymed    = TYMED_HGLOBAL;

    fmts[1].cfFormat = m_cfContents;
    fmts[1].ptd      = nullptr;
    fmts[1].dwAspect = DVASPECT_CONTENT;
    fmts[1].lindex   = -1;
    fmts[1].tymed    = TYMED_ISTREAM;

    return SHCreateStdEnumFmtEtc(2, fmts, ppEnum);
}

STDMETHODIMP VirtualFileDataObject::DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*)
{
    return OLE_E_ADVISENOTSUPPORTED;
}
STDMETHODIMP VirtualFileDataObject::DUnadvise(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
STDMETHODIMP VirtualFileDataObject::EnumDAdvise(IEnumSTATDATA**) { return OLE_E_ADVISENOTSUPPORTED; }

// ── File descriptor ───────────────────────────────────────────────────
HRESULT VirtualFileDataObject::GetFileDescriptor(FORMATETC*, STGMEDIUM* pSTM)
{
    if (m_files.empty()) return DV_E_FORMATETC;

    size_t descSize = sizeof(FILEGROUPDESCRIPTORW) + (m_files.size() - 1) * sizeof(FILEDESCRIPTORW);
    FILEGROUPDESCRIPTORW* fgd = static_cast<FILEGROUPDESCRIPTORW*>(
        GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, descSize));
    if (!fgd) return E_OUTOFMEMORY;

    fgd->cItems = static_cast<UINT>(m_files.size());
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        FILEDESCRIPTORW& fd = fgd->fgd[i];
        fd.dwFlags = FD_UNICODE;
        wcscpy_s(fd.cFileName, MAX_PATH, m_files[i].info.name.c_str());

        fd.nFileSizeLow  = static_cast<DWORD>(m_files[i].info.size & 0xFFFFFFFF);
        fd.nFileSizeHigh = static_cast<DWORD>(m_files[i].info.size >> 32);
        if (m_files[i].info.size != 0)
            fd.dwFlags |= FD_FILESIZE;
    }

    pSTM->tymed          = TYMED_HGLOBAL;
    pSTM->hGlobal        = fgd;
    pSTM->pUnkForRelease = nullptr;
    return S_OK;
}

// Return S_OK with a zero-length stream so the shell doesn't show an error dialog.
static HRESULT emptyStream(STGMEDIUM* pSTM)
{
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return E_OUTOFMEMORY;
    pSTM->tymed          = TYMED_ISTREAM;
    pSTM->pstm           = stream;
    pSTM->pUnkForRelease = nullptr;
    return S_OK;
}

// ── File contents (extracted on demand) ───────────────────────────────
HRESULT VirtualFileDataObject::GetFileContents(FORMATETC* pFE, STGMEDIUM* pSTM)
{
    int idx = pFE->lindex;
    if (idx < 0 || idx >= static_cast<int>(m_files.size()))
        return DV_E_LINDEX;

    if (m_cancelled)
        return emptyStream(pSTM);

    auto& entry = m_files[idx];
    if (!entry.info.engine)
        return E_UNEXPECTED;

    // Show progress dialog on first call
    if (!m_progressDlg)
    {
        uint64_t totalBytes = 0;
        for (const auto& f : m_files)
            totalBytes += f.info.size;
        m_progressDlg = new DragProgressDialog(m_progressTotal, totalBytes, nullptr);
    }

    if (m_progressDlg->wasCancelled())
    {
        m_cancelled = true;
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
        return emptyStream(pSTM);
    }

    // Indeterminate bar during extraction (file counted AFTER extraction)
    m_progressDlg->updateProgress(0, 0,
        QString::fromUtf8(entry.info.archivePath.c_str()));

    // Extract to a temp file using Extract (streams to disk, no memory buffer),
    // then let the shell read from the temp file via FileStream.
    wchar_t tmpDir[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    wchar_t tmpFile[MAX_PATH + 1] = {};
    GetTempFileNameW(tmpDir, L"zfx", 0, tmpFile);
    std::string tmpPath = fs::path(tmpFile).string();

    std::atomic<bool> extractDone{false};
    std::atomic<bool> extractOk{false};

    std::thread extractThread([&]() {
        extractOk = entry.info.engine->Extract(entry.info.archivePath, tmpPath);
        extractDone = true;
    });

    while (!extractDone)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 16);

        if (!m_cancelled && m_progressDlg && m_progressDlg->wasCancelled())
        {
            m_cancelled = true;
            entry.info.engine->cancelExtract();
        }
    }

    if (extractThread.joinable())
        extractThread.join();

    if (m_cancelled)
    {
        fs::remove(tmpPath);
        if (m_progressDlg)
        {
            m_progressDlg->close();
            delete m_progressDlg;
            m_progressDlg = nullptr;
        }
        return emptyStream(pSTM);
    }

    if (!extractOk)
    {
        LOG_ERR("VFDO: Extract failed for %s", entry.info.archivePath.c_str());
        fs::remove(tmpPath);
        return emptyStream(pSTM);
    }

    // Open the temp file for the shell to read
    HANDLE hFile = CreateFileW(tmpFile, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fs::remove(tmpPath);
        return E_FAIL;
    }

    LARGE_INTEGER fileSize = {};
    GetFileSizeEx(hFile, &fileSize);

    // Update progress AFTER successful extraction
    m_progressDlg->finishProgress(
        QString::fromUtf8(entry.info.archivePath.c_str()));

    // Close on last file
    if (m_progressDlg && idx + 1 >= m_progressTotal)
    {
        if (!m_progressDlg->wasCancelled())
        {
            AfterAction aa = m_progressDlg->afterAction();
            if (aa != AfterAction::Nothing)
                ExecuteAfterAction(aa);
        }
        m_progressDlg->close();
        delete m_progressDlg;
        m_progressDlg = nullptr;
    }

    auto* stream = new FileStream(tmpFile, hFile);
    pSTM->tymed          = TYMED_ISTREAM;
    pSTM->pstm           = stream;
    pSTM->pUnkForRelease = nullptr;
    stream->AddRef();

    return S_OK;
}

// ── VirtualDropSource ─────────────────────────────────────────────────
STDMETHODIMP VirtualDropSource::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IDropSource)
    {
        *ppv = static_cast<IDropSource*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) VirtualDropSource::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) VirtualDropSource::Release()
{
    LONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return r;
}
STDMETHODIMP VirtualDropSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState)
{
    if (fEscapePressed) return DRAGDROP_S_CANCEL;
    if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
    return S_OK;
}
STDMETHODIMP VirtualDropSource::GiveFeedback(DWORD) { return DRAGDROP_S_USEDEFAULTCURSORS; }

// ── StartVirtualDrag ───────────────────────────────────────────────────
bool StartVirtualDrag(VirtualFileDataObject* data, HWND hwnd)
{
    data->m_parentHwnd = hwnd;

    VirtualDropSource* source = new VirtualDropSource();
    source->AddRef();

    DWORD effect = 0;
    HRESULT hr = ::DoDragDrop(data, source, DROPEFFECT_COPY, &effect);

    source->Release();
    data->Release();

    return SUCCEEDED(hr);
}

#endif // __WXMSW__
