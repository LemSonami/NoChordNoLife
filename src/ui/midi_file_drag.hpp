#pragma once
#include <windows.h>
#include <ole2.h>
#include <string>
#include <vector>

namespace ncnl {

using FileDragOperation=HRESULT (WINAPI *)(IDataObject*,IDropSource*,DWORD,DWORD*);

// Export by copy, never by move. A successful drop keeps its temporary source
// available for applications which read the file after the OLE operation ends.
HRESULT drag_midi_file(const std::vector<unsigned char>& bytes,const std::wstring& name,
    DWORD* effect=nullptr,FileDragOperation operation=nullptr);

}
