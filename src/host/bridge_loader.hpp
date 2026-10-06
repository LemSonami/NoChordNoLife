#pragma once
#include "bridge_api.hpp"
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace ncnlplug {
inline std::wstring module_file() {
    HMODULE own=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_file),&own)) throw std::runtime_error("无法定位插件模块。");
    wchar_t name[32768]{};
    DWORD count=GetModuleFileNameW(own,name,32768);
    if(!count || count>=32768) throw std::runtime_error("无法定位插件文件。");
    return std::wstring(name,count);
}
inline std::wstring parent_directory(const std::wstring& path) {
    auto separator=path.find_last_of(L"\\/");
    if(separator==std::wstring::npos) throw std::runtime_error("插件目录结构无效，请复制整个 VST3 文件夹。");
    return path.substr(0,separator);
}
inline std::wstring appdata_directory(const std::wstring& roaming) {
    return roaming+L"\\NoChordNoLife\\plugin";
}
inline std::wstring default_data_directory() {
    wchar_t roaming[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,roaming)))
        throw std::runtime_error("无法获取 AppData 用户数据目录。");
    return appdata_directory(roaming);
}
inline std::wstring data_directory() {
    wchar_t override_path[32768]{};
    DWORD count=GetEnvironmentVariableW(L"NCNL_PLUGIN_DATA_DIR",override_path,32768);
    if(count && count<32768) {
        wchar_t absolute[32768]{};
        if(!GetFullPathNameW(override_path,32768,absolute,nullptr)) throw std::runtime_error("插件数据目录无效。");
        int error=SHCreateDirectoryExW(nullptr,absolute,nullptr);
        if(error!=ERROR_SUCCESS && error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS)
            throw std::runtime_error("无法创建指定的插件数据目录。");
        return absolute;
    }
    auto path=default_data_directory();
    int error=SHCreateDirectoryExW(nullptr,path.c_str(),nullptr);
    if(error!=ERROR_SUCCESS && error!=ERROR_ALREADY_EXISTS && error!=ERROR_FILE_EXISTS)
        throw std::runtime_error("无法创建 AppData 插件数据目录，请检查用户目录的写入权限。");
    return path;
}
class NativeBridge {
    HMODULE module=nullptr;
    std::wstring temporary;
public:
    const BridgeApi* api=nullptr;
    HWND window=nullptr;
    ~NativeBridge() { close(); }
    void close() {
        if(api) api->destroy(); api=nullptr; window=nullptr;
        if(module) FreeLibrary(module); module=nullptr;
        if(!temporary.empty()) DeleteFileW(temporary.c_str()); temporary.clear();
    }
    bool attach(HWND parent,const Callbacks& callbacks,const std::vector<unsigned char>& state) {
        try {
            if(!module) {
                std::wstring source=parent_directory(module_file())+L"\\NCNL_UI.dll";
                wchar_t temp[MAX_PATH]{},unique[MAX_PATH]{};
                if(!GetTempPathW(MAX_PATH,temp)||!GetTempFileNameW(temp,L"NCN",0,unique)) throw std::runtime_error("无法创建插件实例临时文件。");
                temporary=unique;
                if(!CopyFileW(source.c_str(),temporary.c_str(),FALSE)) throw std::runtime_error("未找到 NCNL_UI.dll，请复制整个 VST3 文件夹，而不是只复制内部文件。");
                module=LoadLibraryExW(temporary.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
                if(!module) throw std::runtime_error("无法加载插件界面模块。");
                auto get=reinterpret_cast<GetBridgeApi>(GetProcAddress(module,"ncnl_bridge_api"));
                if(!get || !(api=get()) || api->version!=1) throw std::runtime_error("插件界面版本不匹配。");
            }
            auto directory=data_directory();
            window=api->create(parent,directory.c_str(),&callbacks,state.data(),static_cast<std::uint32_t>(state.size()));
            return window!=nullptr;
        } catch(const std::exception& e) {
            std::string text=e.what();
            int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
            std::wstring wide(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),&wide[0],n);
            MessageBoxW(parent,wide.c_str(),L"插件界面加载失败",MB_OK|MB_ICONERROR); close(); return false;
        }
    }
};
}
