#pragma once
#include "../resources/embedded_assets.hpp"
#include <stdexcept>

namespace ncnlplug {
inline void release_default_file(const std::wstring& destination,const BYTE* bytes,DWORD size,DWORD attributes=FILE_ATTRIBUTE_NORMAL) {
    HANDLE file=CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,attributes,nullptr);
    if(file==INVALID_HANDLE_VALUE) {
        DWORD error=GetLastError();
        if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS) {
            DWORD existing=GetFileAttributesW(destination.c_str());
            if(existing!=INVALID_FILE_ATTRIBUTES && !(existing&FILE_ATTRIBUTE_DIRECTORY)) return;
        }
        throw std::runtime_error("无法写入默认预设，请检查插件用户数据目录的写入权限。");
    }
    DWORD written=0;
    bool saved=WriteFile(file,bytes,size,&written,nullptr) && written==size;
    if(saved) saved=FlushFileBuffers(file)!=FALSE;
    CloseHandle(file);
    if(!saved) {
        DeleteFileW(destination.c_str());
        throw std::runtime_error("默认预设保存失败，请检查插件用户数据目录的权限及磁盘空间。");
    }
}
inline void initialize_default_presets(const std::wstring& directory) {
    auto folder=directory+L"\\presents\\";
    auto marker=folder+L".defaults-initialized";
    if(GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES) return;
    for(const auto* name:{L"Chop01.mid",L"Chop02.mid",L"Swing01.mid",L"Swing02.mid",L"order.txt"}) {
        auto bytes=ncnl::embedded_asset(std::wstring(L":/assets/defaults/presents/")+name);
        if(!bytes.data || !bytes.size) throw std::runtime_error("插件内置的默认预设不完整，请重新安装完整的 VST3 文件夹。");
        release_default_file(folder+name,bytes.data,bytes.size);
    }
    const BYTE version[]={'1','\n'};
    release_default_file(marker,version,sizeof(version),FILE_ATTRIBUTE_HIDDEN);
}
}
