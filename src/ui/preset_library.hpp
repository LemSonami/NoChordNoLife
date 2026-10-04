#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <functional>
#include <string>
#include <vector>
#include "mouse_feedback.hpp"

namespace ncnl {
class PresetStore {
public:
    explicit PresetStore(const std::wstring& directory);
    void refresh();
    const std::vector<std::wstring>& files() const { return entries; }
    std::wstring path(std::size_t index) const;
    std::size_t save(const std::wstring& name,const std::vector<unsigned char>& midi);
    void rename(std::size_t index,const std::wstring& name);
    void reorder(std::size_t from,std::size_t to);
    void recycle(std::size_t index,HWND owner);
    int undo_recycle();
    static std::wstring display_name(const std::wstring& file);
private:
    void persist_order();
    std::wstring directory;
    std::vector<std::wstring> entries;
    struct DeletedPreset {
        std::wstring file;
        std::vector<unsigned char> midi;
        std::size_t position;
    };
    std::vector<DeletedPreset> deletion_history;
};

using PresetLoad=std::function<bool(const std::wstring&)>;
using PresetSave=std::function<std::vector<unsigned char>()>;
void open_preset_library(HWND owner,const std::wstring& directory,const std::wstring& cursor_directory,
    Gdiplus::FontFamily* font,const std::wstring& font_name,PresetLoad load,PresetSave save,
    const std::wstring& skin_path=L"",const std::wstring& button_directory=L"");
HWND preset_library_window();
MouseFeedback& preset_library_feedback();
void close_preset_library();
}