#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "plugin_manager.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace ncnl {
namespace {
std::wstring ini(const std::wstring& file,const wchar_t* key,const wchar_t* fallback=L"") {
    HANDLE handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (handle==INVALID_HANDLE_VALUE) { return fallback; }
    DWORD size=GetFileSize(handle,nullptr),read=0;
    if (size==INVALID_FILE_SIZE || size>65536) { CloseHandle(handle); return fallback; }
    std::string bytes(size,'\0'); BOOL ok=ReadFile(handle,size?&bytes[0]:nullptr,size,&read,nullptr); CloseHandle(handle);
    if (!ok || read!=size) { return fallback; }
    if (bytes.compare(0,3,"\xef\xbb\xbf")==0) { bytes.erase(0,3); }
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),bytes.size(),nullptr,0);
    if (!length) { return fallback; }
    std::wstring text(length,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),bytes.size(),&text[0],length);
    auto trim=[](const std::wstring& s) {
        auto first=s.find_first_not_of(L" \t\r");
        return first==std::wstring::npos ? std::wstring() : s.substr(first,s.find_last_not_of(L" \t\r")-first+1);
    };
    std::wistringstream stream(text); std::wstring line; bool plugin=false;
    while (std::getline(stream,line)) {
        line=trim(line);
        if (line.empty() || line[0]==L';' || line[0]==L'#') { continue; }
        if (line[0]==L'[') { plugin=line==L"[plugin]"; continue; }
        auto equal=line.find(L'=');
        if (plugin && equal!=std::wstring::npos && trim(line.substr(0,equal))==key) { return trim(line.substr(equal+1)); }
    }
    return fallback;
}
bool regular(const std::wstring& file,bool directory=false) {
    DWORD a=GetFileAttributesW(file.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_REPARSE_POINT) &&
        ((a&FILE_ATTRIBUTE_DIRECTORY)!=0)==directory;
}
bool filename(const std::wstring& text,const wchar_t* extension) {
    auto dot=text.find_last_of(L'.');
    return !text.empty() && text.size()<128 && text.find_first_of(L"/\\:")==std::wstring::npos &&
        dot!=std::wstring::npos && lstrcmpiW(text.c_str()+dot,extension)==0;
}
bool identifier(const std::wstring& text) {
    return !text.empty() && text.size()<=48 && std::all_of(text.begin(),text.end(),[](wchar_t c) {
        return (c>=L'a'&&c<=L'z') || (c>=L'0'&&c<=L'9') || c==L'_' || c==L'-';
    });
}
PluginInfo builtin() {
    PluginInfo info; info.id="ncnl"; info.name=L"NCNL"; info.version=L"内置";
    info.description=L"和弦生成、情感色彩、钢琴卷帘与节奏型预设";
    info.enabled=info.builtin=true; info.status=L"始终启用"; return info;
}
}
PluginManager::PluginManager() { items.push_back(builtin()); }
PluginManager::~PluginManager() { shutdown(); }
void PluginManager::close_page() {
    if (page) {
        try { if (api.activate_page) { api.activate_page(page,0); } api.destroy_page(page); }
        catch (...) { if (IsWindow(page)) { DestroyWindow(page); } }
        if (IsWindow(page)) { DestroyWindow(page); } page=nullptr;
    }
    if (module) { FreeLibrary(module); module=nullptr; }
    api={}; active=0;
}
void PluginManager::shutdown() { close_page(); }
void PluginManager::scan(const std::wstring& directory) {
    close_page(); items.clear(); items.push_back(builtin());
    wchar_t full[32768]={}; DWORD length=GetFullPathNameW(directory.c_str(),32768,full,nullptr);
    if (!length || length>=32768) { throw std::runtime_error("插件目录路径无效。"); }
    root=full;
    if (!regular(root,true) && !CreateDirectoryW(root.c_str(),nullptr)) {
        throw std::runtime_error("无法创建插件目录。");
    }
    std::vector<std::wstring> folders;
    WIN32_FIND_DATAW data={}; HANDLE search=FindFirstFileW((root+L"\\*").c_str(),&data);
    if (search!=INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
                wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L"..")) { folders.push_back(data.cFileName); }
        } while (FindNextFileW(search,&data)); FindClose(search);
    }
    std::sort(folders.begin(),folders.end());
    for (const auto& folder:folders) {
        auto base=root+L"\\"+folder,manifest=base+L"\\plugin.ini";
        if (!regular(manifest)) { continue; }
        auto id=ini(manifest,L"id"),entry=ini(manifest,L"entry",L"plugin.dll");
        if (!identifier(id) || id==L"ncnl" || !filename(entry,L".dll")) { continue; }
        PluginInfo info; info.id=std::string(id.begin(),id.end());
        if (std::any_of(items.begin(),items.end(),[&](const PluginInfo& old){return old.id==info.id;})) { continue; }
        info.name=ini(manifest,L"name",id.c_str()); info.version=ini(manifest,L"version",L"1.0.0");
        info.description=ini(manifest,L"description",L"NCNL 功能扩展"); info.folder=base; info.entry=base+L"\\"+entry;
        auto button=ini(manifest,L"button",L"button.png");
        if (filename(button,L".png") && regular(base+L"\\"+button)) { info.button=base+L"\\"+button; }
        bool valid=ini(manifest,L"abi")==L"1" && regular(info.entry);
        if (!valid) { info.status=L"接口版本不兼容或 DLL 缺失"; }
        else {
            auto state=root+L"\\state.ini";
            info.enabled=GetPrivateProfileIntW(L"enabled",id.c_str(),0,state.c_str())==1;
            if (info.enabled && enabled_plugins().size()>=4) { info.enabled=false; }
            info.status=info.enabled ? L"已启用 · 切换时加载" : L"未启用";
        }
        items.push_back(std::move(info));
        if (items.size()>=32) { break; }
    }
}
std::vector<std::size_t> PluginManager::enabled_plugins() const {
    std::vector<std::size_t> result;
    for (std::size_t i=0;i<items.size();++i) { if (items[i].enabled) { result.push_back(i); } }
    return result;
}
bool PluginManager::set_enabled(std::size_t index,bool enabled,std::wstring& error) {
    if (index>=items.size()) { error=L"没有找到这个插件。"; return false; }
    auto& info=items[index];
    if (info.builtin) { error=L"NCNL 是内置插件，不能禁用。"; return false; }
    if (enabled && (ini(info.folder+L"\\plugin.ini",L"abi")!=L"1" || !regular(info.entry))) {
        error=L"插件接口不兼容或 DLL 缺失。"; return false;
    }
    if (enabled && !info.enabled && enabled_plugins().size()>=4) {
        error=L"最多同时启用四个插件（包含 NCNL）。"; return false;
    }
    std::wstring id(info.id.begin(),info.id.end());
    if (!WritePrivateProfileStringW(L"enabled",id.c_str(),enabled?L"1":L"0",(root+L"\\state.ini").c_str())) {
        error=L"无法保存插件启用状态。"; return false;
    }
    if (!enabled && active==index) { close_page(); }
    info.enabled=enabled; info.status=enabled ? L"已启用 · 切换时加载" : L"未启用"; return true;
}
bool PluginManager::activate(std::size_t index,HWND parent,const NcnlHostV1& host,std::wstring& error) {
    if (index>=items.size() || !items[index].enabled) { error=L"插件未启用。"; return false; }
    if (index==active) { return true; }
    close_page();
    if (index==0) { return true; }
    auto& info=items[index];
    module=LoadLibraryExW(info.entry.c_str(),nullptr,0x00000100|0x00000800);
    if (!module) { info.status=error=L"加载失败：请检查 DLL 位数、依赖文件及系统版本。"; return false; }
    auto query=reinterpret_cast<NcnlGetPluginV1>(GetProcAddress(module,"ncnl_get_plugin"));
    host_api=host; api.size=sizeof(api);
    try {
        if (!query || !query(NCNL_PLUGIN_ABI,&api) || api.size!=sizeof(api) || api.abi!=NCNL_PLUGIN_ABI ||
            !api.id || info.id!=api.id || !api.create_page || !api.destroy_page || !api.resize_page) {
            throw std::runtime_error("插件接口不完整或插件编号不一致。");
        }
        auto created=static_cast<HWND>(api.create_page(parent,&host_api));
        if (!created || !IsWindow(created) || GetParent(created)!=parent || !(GetWindowLongPtrW(created,GWL_STYLE)&WS_CHILD)) {
            throw std::runtime_error("插件没有创建有效的子页面。");
        }
        page=created;
        active=index; ShowWindow(page,SW_SHOW);
        if (api.activate_page) { api.activate_page(page,1); }
        info.status=L"已加载"; return true;
    } catch (...) {
        close_page(); info.status=error=L"插件初始化失败：接口或页面无效。"; return false;
    }
}
void PluginManager::resize(int width,int height) {
    if (page) { api.resize_page(page,width,height); }
}
}
