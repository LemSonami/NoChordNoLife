#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "plugin_api.h"

namespace ncnl {
struct PluginInfo {
    std::string id;
    std::wstring name,version,description,folder,entry,button,status;
    bool enabled=false,builtin=false;
};
class PluginManager {
public:
    PluginManager();
    ~PluginManager();
    PluginManager(const PluginManager&)=delete;
    PluginManager& operator=(const PluginManager&)=delete;
    void scan(const std::wstring& directory);
    const std::vector<PluginInfo>& plugins() const { return items; }
    std::vector<std::size_t> enabled_plugins() const;
    bool set_enabled(std::size_t index,bool enabled,std::wstring& error);
    bool activate(std::size_t index,HWND parent,const NcnlHostV1& host,std::wstring& error);
    void resize(int width,int height);
    void shutdown();
    std::size_t active_index() const { return active; }
    const std::wstring& directory() const { return root; }
private:
    void close_page();
    std::wstring root;
    std::vector<PluginInfo> items;
    std::size_t active=0;
    HMODULE module=nullptr;
    HWND page=nullptr;
    NcnlPluginV1 api={};
    NcnlHostV1 host_api={};
};
}
