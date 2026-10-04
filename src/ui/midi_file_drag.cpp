#include "midi_file_drag.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>

namespace ncnl {
namespace {

class FormatEnumerator final : public IEnumFORMATETC {
    LONG references=1;
    FORMATETC formats[2];
    ULONG position;
public:
    explicit FormatEnumerator(UINT preferred,ULONG cursor=0):position(cursor) {
        formats[0]={CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        formats[1]={static_cast<CLIPFORMAT>(preferred),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** object) override {
        if (!object) { return E_POINTER; }
        *object=nullptr;
        if (id!=IID_IUnknown && id!=IID_IEnumFORMATETC) { return E_NOINTERFACE; }
        *object=static_cast<IEnumFORMATETC*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining=InterlockedDecrement(&references);
        if (!remaining) { delete this; } return remaining;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count,FORMATETC* output,ULONG* fetched) override {
        if (!output || (!fetched && count!=1)) { return E_POINTER; }
        ULONG actual=0;
        while (actual<count && position<2) { output[actual++]=formats[position++]; }
        if (fetched) { *fetched=actual; }
        return actual==count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        ULONG actual=std::min(count,2-position); position+=actual;
        return actual==count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { position=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** output) override {
        if (!output) { return E_POINTER; }
        *output=new(std::nothrow) FormatEnumerator(formats[1].cfFormat,position);
        return *output ? S_OK : E_OUTOFMEMORY;
    }
};

class MidiFileData final : public IDataObject {
    LONG references=1;
    std::wstring path;
    UINT preferred=RegisterClipboardFormatW(L"Preferred DropEffect");
public:
    explicit MidiFileData(const std::wstring& file):path(file) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** object) override {
        if (!object) { return E_POINTER; }
        *object=nullptr;
        if (id!=IID_IUnknown && id!=IID_IDataObject) { return E_NOINTERFACE; }
        *object=static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining=InterlockedDecrement(&references);
        if (!remaining) { delete this; } return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) { return E_POINTER; }
        if (format->dwAspect!=DVASPECT_CONTENT) { return DV_E_DVASPECT; }
        if (format->lindex!=-1) { return DV_E_LINDEX; }
        if (!(format->tymed&TYMED_HGLOBAL)) { return DV_E_TYMED; }
        if (format->cfFormat!=CF_HDROP && format->cfFormat!=preferred) { return DV_E_FORMATETC; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format,STGMEDIUM* medium) override {
        if (!medium) { return E_POINTER; }
        *medium={}; HRESULT result=QueryGetData(format);
        if (FAILED(result)) { return result; }
        bool files=format->cfFormat==CF_HDROP;
        SIZE_T size=files ? sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t) : sizeof(DWORD);
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,size);
        if (!memory) { return E_OUTOFMEMORY; }
        void* buffer=GlobalLock(memory);
        if (!buffer) { GlobalFree(memory); return E_OUTOFMEMORY; }
        if (files) {
            auto drop=static_cast<DROPFILES*>(buffer);
            drop->pFiles=sizeof(DROPFILES); drop->fWide=TRUE;
            std::memcpy(static_cast<unsigned char*>(buffer)+sizeof(DROPFILES),
                path.c_str(),(path.size()+1)*sizeof(wchar_t));
        } else { *static_cast<DWORD*>(buffer)=DROPEFFECT_COPY; }
        GlobalUnlock(memory);
        medium->tymed=TYMED_HGLOBAL; medium->hGlobal=memory; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* output) override {
        if (!output) { return E_POINTER; } output->ptd=nullptr; return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** output) override {
        if (!output) { return E_POINTER; } *output=nullptr;
        if (direction!=DATADIR_GET) { return E_NOTIMPL; }
        *output=new(std::nothrow) FormatEnumerator(preferred);
        return *output ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

class MidiDropSource final : public IDropSource {
    LONG references=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** object) override {
        if (!object) { return E_POINTER; } *object=nullptr;
        if (id!=IID_IUnknown && id!=IID_IDropSource) { return E_NOINTERFACE; }
        *object=static_cast<IDropSource*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining=InterlockedDecrement(&references);
        if (!remaining) { delete this; } return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        if (escape || (keys&(MK_RBUTTON|MK_MBUTTON))) { return DRAGDROP_S_CANCEL; }
        return keys&MK_LBUTTON ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

struct TemporaryMidi {
    std::wstring directory,path;
    bool keep=false;
    ~TemporaryMidi() {
        if (!keep) {
            if (!path.empty()) { DeleteFileW(path.c_str()); }
            if (!directory.empty()) { RemoveDirectoryW(directory.c_str()); }
        }
    }
};
struct OleScope {
    HRESULT result=OleInitialize(nullptr);
    ~OleScope() { if (SUCCEEDED(result)) { OleUninitialize(); } }
};

}

HRESULT drag_midi_file(const std::vector<unsigned char>& bytes,const std::wstring& name,
    DWORD* effect,FileDragOperation operation) {
    if (effect) { *effect=DROPEFFECT_NONE; }
    if (bytes.size()<14 || std::memcmp(bytes.data(),"MThd",4)!=0 ||
        bytes.size()>std::numeric_limits<DWORD>::max()) { return E_INVALIDARG; }
    OleScope ole;
    if (FAILED(ole.result)) { return ole.result; }
    wchar_t temp[MAX_PATH]={},unique[MAX_PATH]={};
    DWORD length=GetTempPathW(MAX_PATH,temp);
    if (length>=MAX_PATH) { return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
    if (!length || !GetTempFileNameW(temp,L"NCN",0,unique)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!DeleteFileW(unique)) { return HRESULT_FROM_WIN32(GetLastError()); }
    if (!CreateDirectoryW(unique,nullptr)) { return HRESULT_FROM_WIN32(GetLastError()); }
    TemporaryMidi file; file.directory=unique;
    std::wstring safe=name.substr(0,64);
    for (auto& character:safe) {
        if (character<32 || std::wcschr(L"<>:\"/\\|?*",character)) { character=L'_'; }
    }
    file.path=file.directory+L"\\NCNL_"+(safe.empty() ? L"和弦" : safe)+L".mid";
    HANDLE handle=CreateFileW(file.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (handle==INVALID_HANDLE_VALUE) { return HRESULT_FROM_WIN32(GetLastError()); }
    DWORD written=0;
    BOOL saved=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
    DWORD error=GetLastError(); CloseHandle(handle);
    if (!saved || written!=bytes.size()) { return HRESULT_FROM_WIN32(saved ? ERROR_WRITE_FAULT : error); }
    auto data=new(std::nothrow) MidiFileData(file.path);
    auto source=new(std::nothrow) MidiDropSource;
    if (!data || !source) {
        if (data) { data->Release(); } if (source) { source->Release(); } return E_OUTOFMEMORY;
    }
    DWORD actual=DROPEFFECT_NONE;
    HRESULT result=(operation ? operation : DoDragDrop)(data,source,DROPEFFECT_COPY,&actual);
    data->Release(); source->Release();
    file.keep=result==DRAGDROP_S_DROP && (actual&DROPEFFECT_COPY);
    if (effect) { *effect=actual; }
    return result;
}

}
