#include "../host/runtime.hpp"
#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <memory>
#include <string>
#include <asset_index.hpp>

namespace ncnl {
struct AssetBytes { const BYTE* data; DWORD size; };
inline AssetBytes embedded_asset(const std::wstring& path) {
    const std::wstring prefix=L":/assets/";
    auto normalized=path; std::replace(normalized.begin(),normalized.end(),L'\\',L'/');
    if (normalized.compare(0,prefix.size(),prefix)!=0) { return {nullptr,0}; }
    auto name=normalized.substr(prefix.size());
    for (const auto& entry:ASSET_INDEX) { if (name==entry.path) {
        HMODULE module=ncnl::runtime_module();
        HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(entry.id),MAKEINTRESOURCEW(10));
        if (!resource) { return {nullptr,0}; }
        return {static_cast<const BYTE*>(LockResource(LoadResource(module,resource))),SizeofResource(module,resource)};
    } }
    return {nullptr,0};
}
struct ImageDeleter {
    IStream* stream=nullptr;
    void operator()(Gdiplus::Image* image) const { delete image; if (stream) { stream->Release(); } }
};
using AssetImage=std::unique_ptr<Gdiplus::Image,ImageDeleter>;
inline AssetImage asset_image(const std::wstring& path) {
    if (path.compare(0,2,L":/")==0) {
        auto bytes=embedded_asset(path); if (!bytes.data || !bytes.size) { return {}; }
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes.size); if (!memory) { return {}; }
        void* target=GlobalLock(memory); if (!target) { GlobalFree(memory); return {}; }
        CopyMemory(target,bytes.data,bytes.size); GlobalUnlock(memory);
        IStream* stream=nullptr;
        if (CreateStreamOnHGlobal(memory,TRUE,&stream)!=S_OK) { GlobalFree(memory); return {}; }
        ImageDeleter deleter; deleter.stream=stream;
        AssetImage image(new Gdiplus::Image(stream),deleter);
        if (image->GetLastStatus()!=Gdiplus::Ok) { return {}; } return image;
    }
    AssetImage image(new Gdiplus::Image(path.c_str()));
    if (image->GetLastStatus()!=Gdiplus::Ok) { return {}; } return image;
}
inline int application_cursor_size(int metric) { return MulDiv(GetSystemMetrics(metric),5,4); }
inline HCURSOR asset_cursor(const std::wstring& path) {
    auto bytes=embedded_asset(path);
    if (!bytes.data) { return nullptr; }
    return static_cast<HCURSOR>(CreateIconFromResourceEx(const_cast<BYTE*>(bytes.data),bytes.size,FALSE,0x00030000,
        application_cursor_size(SM_CXCURSOR),application_cursor_size(SM_CYCURSOR),LR_DEFAULTSIZE));
}
}
