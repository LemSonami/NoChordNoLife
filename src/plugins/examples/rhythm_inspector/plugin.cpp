#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600
#define NCNL_BUILD_PLUGIN
#include <windows.h>
#include <sstream>
#include <algorithm>
#include "../../plugin_api.h"

namespace {
struct Page { NcnlHostV1 host; HFONT font; };
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto data=reinterpret_cast<Page*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_CREATE) {
        auto create=reinterpret_cast<CREATESTRUCTW*>(l); data=static_cast<Page*>(create->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data)); return 0;
    }
    if (message==WM_TIMER) { InvalidateRect(window,nullptr,FALSE); return 0; }
    if (message==WM_ERASEBKGND) { return 1; }
    if (message==WM_PAINT && data) {
        PAINTSTRUCT paint={}; HDC dc=BeginPaint(window,&paint); RECT box={}; GetClientRect(window,&box);
        HBRUSH fill=CreateSolidBrush(RGB(25,45,55)); FillRect(dc,&box,fill); DeleteObject(fill);
        NcnlContextV1 context={}; context.size=sizeof(context);
        std::wostringstream text; text<<L"节奏观察\n\n";
        if (data->host.get_context(data->host.user,&context)) {
            wchar_t mode[64]={}; MultiByteToWideChar(CP_UTF8,0,context.mode,-1,mode,64);
            text<<L"当前调式："<<mode<<L"\n\n速度："<<context.bpm
                <<L" BPM\n\n节奏事件："<<context.rhythm_events<<L"\n\nMIDI 音符："<<context.midi_notes;
        } else { text<<L"暂时无法读取主程序数据。"; }
        HGDIOBJ previous=SelectObject(dc,data->font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(220,244,231));
        box.left+=30; box.top+=28; box.right-=30;
        DrawTextW(dc,text.str().c_str(),-1,&box,DT_LEFT|DT_WORDBREAK);
        SelectObject(dc,previous); EndPaint(window,&paint); return 0;
    }
    if (message==WM_NCDESTROY && data) { KillTimer(window,1); DeleteObject(data->font); delete data; }
    return DefWindowProcW(window,message,w,l);
}
void* NCNL_CALL create(void* parent,const NcnlHostV1* host) {
    if (!host || host->abi!=NCNL_PLUGIN_ABI || !host->get_context) { return nullptr; }
    HINSTANCE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&procedure),&module);
    WNDCLASSW type={}; type.hInstance=module; type.lpszClassName=L"NcnlRhythmInspector"; type.lpfnWndProc=procedure;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW); RegisterClassW(&type);
    auto data=new Page{*host,CreateFontW(-26,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI")};
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_CHILD,0,0,1,1,static_cast<HWND>(parent),nullptr,module,data);
    if (!window) { DeleteObject(data->font); delete data; UnregisterClassW(type.lpszClassName,module); } return window;
}
void NCNL_CALL destroy(void* page) {
    HWND window=static_cast<HWND>(page); HINSTANCE module=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
    DestroyWindow(window); UnregisterClassW(L"NcnlRhythmInspector",module);
}
void NCNL_CALL resize(void* page,int32_t width,int32_t height) {
    HWND window=static_cast<HWND>(page); auto data=reinterpret_cast<Page*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    HFONT font=CreateFontW(-std::max(16,static_cast<int>(26.0*width/900)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    HFONT previous=data->font; data->font=font; DeleteObject(previous); MoveWindow(window,0,0,width,height,TRUE);
}
void NCNL_CALL activate(void* page,int32_t enabled) {
    if (enabled) { SetTimer(static_cast<HWND>(page),1,250,nullptr); }
    else { KillTimer(static_cast<HWND>(page),1); }
}
}
extern "C" NCNL_EXPORT int32_t NCNL_CALL ncnl_get_plugin(uint32_t host_abi,NcnlPluginV1* output) {
    if (host_abi!=NCNL_PLUGIN_ABI || !output || output->size<sizeof(NcnlPluginV1)) { return 0; }
    *output={sizeof(NcnlPluginV1),NCNL_PLUGIN_ABI,"rhythm_inspector",create,destroy,resize,activate}; return 1;
}
