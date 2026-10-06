#pragma once
#include <windows.h>
#include <string>

namespace ncnl {
inline HMODULE runtime_module() {
    HMODULE result=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&runtime_module),&result);
    return result;
}
inline std::wstring& runtime_directory() {
    static std::wstring value;
    return value;
}
}
