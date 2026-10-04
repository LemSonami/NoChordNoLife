#pragma once
#include <windows.h>
#include <ole2.h>
#include <string>
#include <vector>

namespace ncnl {

using FileDragOperation=HRESULT (WINAPI *)(IDataObject*,IDropSource*,DWORD,DWORD*);



HRESULT drag_midi_file(const std::vector<unsigned char>& bytes,const std::wstring& name,
    DWORD* effect=nullptr,FileDragOperation operation=nullptr);

}