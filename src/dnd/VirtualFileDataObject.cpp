#ifdef __WXMSW__

#include "VirtualFileDataObject.h"

#include <wx/log.h>
#include <wx/window.h>
#include <wx/msw/private.h>
#include <algorithm>
#include <shlobj.h>

#include "engine/ArchiveEngine.h"

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

// Simple IStream that wraps a buffer (takes ownership of the data)
class MemStream : public IStream
{
    std::vector<uint8_t> m_owned;
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    ULONG m_ref = 1;

public:
    MemStream(std::vector<uint8_t>&& data)
        : m_owned(std::move(data)), m_data(m_owned.data()), m_size(m_owned.size()) {}

    // IUnknown
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
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    // IStream
    STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead) override
    {
        ULONG avail = static_cast<ULONG>(m_size - m_pos);
        ULONG toRead = std::min(cb, avail);
        memcpy(pv, m_data + m_pos, toRead);
        m_pos += toRead;
        if (pcbRead) *pcbRead = toRead;
        return toRead == cb ? S_OK : S_FALSE;
    }
    STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPos) override
    {
        switch (dwOrigin)
        {
        case STREAM_SEEK_SET: m_pos = static_cast<size_t>(dlibMove.QuadPart); break;
        case STREAM_SEEK_CUR: m_pos += static_cast<size_t>(dlibMove.QuadPart); break;
        case STREAM_SEEK_END: m_pos = m_size + static_cast<size_t>(dlibMove.QuadPart); break;
        }
        if (m_pos > m_size) m_pos = m_size;
        if (plibNewPos) plibNewPos->QuadPart = m_pos;
        return S_OK;
    }
    STDMETHODIMP Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    STDMETHODIMP Commit(DWORD) override { return S_OK; }
    STDMETHODIMP Revert() override { return STG_E_REVERTED; }
    STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
    STDMETHODIMP Stat(STATSTG* pStat, DWORD) override
    {
        memset(pStat, 0, sizeof(*pStat));
        pStat->cbSize.QuadPart = m_size;
        pStat->type = STGTY_STREAM;
        return S_OK;
    }
    STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }

private:
    ~MemStream() = default;
};

// ── VirtualFileDataObject ─────────────────────────────────────────────

VirtualFileDataObject::VirtualFileDataObject()
{
    m_cfDescriptor = GetFileDescriptorFormat();
    m_cfContents   = GetFileContentsFormat();
}

VirtualFileDataObject::~VirtualFileDataObject() = default;

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

STDMETHODIMP_(ULONG) VirtualFileDataObject::AddRef() { return ++m_refCount; }
STDMETHODIMP_(ULONG) VirtualFileDataObject::Release()
{
    ULONG r = --m_refCount;
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

// ── File contents (extracted on demand) ───────────────────────────────
HRESULT VirtualFileDataObject::GetFileContents(FORMATETC* pFE, STGMEDIUM* pSTM)
{
    int idx = pFE->lindex;
    if (idx < 0 || idx >= static_cast<int>(m_files.size()))
        return DV_E_LINDEX;

    auto& entry = m_files[idx];
    if (!entry.info.engine)
        return E_UNEXPECTED;

    // Show progress dialog on first call
    if (!m_progressDlg && m_parentHwnd)
    {
        wxWindow* parent = wxFindWinFromHandle(m_parentHwnd);
        if (parent)
        {
            m_progressDlg = new wxProgressDialog(
                _("Extracting files..."),
                _("Extracting..."),
                m_progressTotal, parent,
                wxPD_AUTO_HIDE);
        }
    }

    wxLogDebug("VFDO: extracting %s on demand (%d/%d)",
               entry.info.archivePath, idx + 1, m_progressTotal);

    auto data = entry.info.engine->ReadFile(entry.info.archivePath);
    if (data.empty())
    {
        wxLogWarning("VFDO: failed to extract %s", entry.info.archivePath);
        return E_FAIL;
    }

    // Update progress
    if (m_progressDlg)
    {
        m_progressDlg->Update(idx + 1,
            wxString::Format(_("Extracting: %s"),
                wxString::FromUTF8(entry.info.archivePath)));
    }

    auto* stream = new MemStream(std::move(data));
    pSTM->tymed          = TYMED_ISTREAM;
    pSTM->pstm           = stream;
    pSTM->pUnkForRelease = nullptr;
    stream->AddRef();

    // Close dialog on last file
    if (m_progressDlg && idx + 1 >= m_progressTotal)
    {
        m_progressDlg->Update(m_progressTotal, _("Done."));
        m_progressDlg->Destroy();
        m_progressDlg = nullptr;
    }

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
STDMETHODIMP_(ULONG) VirtualDropSource::AddRef() { return ++m_ref; }
STDMETHODIMP_(ULONG) VirtualDropSource::Release()
{
    ULONG r = --m_ref; if (r == 0) delete this; return r;
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

    // Clean up progress dialog if still open (e.g. drag cancelled)
    if (data->m_progressDlg)
    {
        data->m_progressDlg->Destroy();
        data->m_progressDlg = nullptr;
    }

    source->Release();
    data->Release();

    return SUCCEEDED(hr);
}

#endif // __WXMSW__
