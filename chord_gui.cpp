#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "chord_algorithms.hpp"

namespace {

constexpr int ID_ANALYSIS_TYPE=1001;
constexpr int ID_MODE_INPUT=1002;
constexpr int ID_CHORD1_INPUT=1003;
constexpr int ID_CHORD2_INPUT=1004;
constexpr int ID_CALCULATE=1005;

HWND analysis_type=nullptr;
HWND chord1_label=nullptr;
HWND chord2_label=nullptr;
HWND mode_input=nullptr;
HWND chord1_input=nullptr;
HWND chord2_input=nullptr;

std::string wide_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    int size=WideCharToMultiByte(
        CP_UTF8,0,value.data(),static_cast<int>(value.size()),
        nullptr,0,nullptr,nullptr
    );
    std::string result(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(
        CP_UTF8,0,value.data(),static_cast<int>(value.size()),
        &result[0],size,nullptr,nullptr
    );
    return result;
}

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int size=MultiByteToWideChar(
        CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        nullptr,0
    );
    if (size<=0) {
        return L"无法显示错误信息";
    }
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    MultiByteToWideChar(
        CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        &result[0],size
    );
    return result;
}

std::wstring get_window_text(HWND window) {
    int length=GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length+1),L'\0');
    if (length>0) {
        GetWindowTextW(window,&text[0],length+1);
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void set_default_font(HWND window) {
    SendMessageW(
        window,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
        TRUE
    );
}

HWND create_control(
    const wchar_t* class_name,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    HWND parent,
    int id=0
) {
    HWND control=CreateWindowExW(
        0,class_name,text,style,
        x,y,width,height,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr
    );
    set_default_font(control);
    return control;
}

void update_form() {
    int selected=static_cast<int>(SendMessageW(analysis_type,CB_GETCURSEL,0,0));
    bool progression=selected==1;
    SetWindowTextW(chord1_label,progression ? L"第一个和弦" : L"和弦内音");
    ShowWindow(chord2_label,progression ? SW_SHOW : SW_HIDE);
    ShowWindow(chord2_input,progression ? SW_SHOW : SW_HIDE);
}

void calculate_and_show(HWND owner) {
    try {
        std::string mode=wide_to_utf8(get_window_text(mode_input));
        std::string chord1=wide_to_utf8(get_window_text(chord1_input));
        int selected=static_cast<int>(SendMessageW(analysis_type,CB_GETCURSEL,0,0));

        double score;
        std::wstring title;
        if (selected==1) {
            std::string chord2=wide_to_utf8(get_window_text(chord2_input));
            score=ncnl::chord_progression_score(mode,chord1,chord2);
            title=L"和弦进行评分";
        }
        else {
            score=ncnl::chord_emotion_score(mode,chord1);
            title=L"和弦情感评分";
        }

        std::wostringstream message;
        message<<L"计算结果："<<std::fixed<<std::setprecision(2)<<score;
        MessageBoxW(owner,message.str().c_str(),title.c_str(),MB_OK|MB_ICONINFORMATION);
    }
    catch (const std::exception& error) {
        std::wstring message=utf8_to_wide(error.what());
        MessageBoxW(owner,message.c_str(),L"输入错误",MB_OK|MB_ICONERROR);
    }
}

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    switch (message) {
    case WM_CREATE: {
        create_control(
            L"STATIC",L"NoChordNoLife C++ 算法演示",
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            30,22,500,24,window
        );

        create_control(
            L"STATIC",L"分析模块",
            WS_CHILD|WS_VISIBLE,
            42,70,110,24,window
        );
        analysis_type=create_control(
            L"COMBOBOX",L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,
            165,66,340,160,window,ID_ANALYSIS_TYPE
        );
        SendMessageW(analysis_type,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"和弦情感色彩"));
        SendMessageW(analysis_type,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"和弦进行"));
        SendMessageW(analysis_type,CB_SETCURSEL,0,0);

        create_control(
            L"STATIC",L"调式",
            WS_CHILD|WS_VISIBLE,
            42,120,110,24,window
        );
        mode_input=create_control(
            L"EDIT",L"C Ionian",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,
            165,116,340,28,window,ID_MODE_INPUT
        );

        chord1_label=create_control(
            L"STATIC",L"和弦内音",
            WS_CHILD|WS_VISIBLE,
            42,170,110,24,window
        );
        chord1_input=create_control(
            L"EDIT",L"C E G",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,
            165,166,340,28,window,ID_CHORD1_INPUT
        );

        chord2_label=create_control(
            L"STATIC",L"第二个和弦",
            WS_CHILD,
            42,220,110,24,window
        );
        chord2_input=create_control(
            L"EDIT",L"G B D",
            WS_CHILD|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,
            165,216,340,28,window,ID_CHORD2_INPUT
        );

        create_control(
            L"BUTTON",L"计算",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            205,278,150,38,window,ID_CALCULATE
        );

        create_control(
            L"STATIC",L"音名示例：C C# Db；调式名称区分大小写",
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            30,340,500,22,window
        );
        update_form();
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(w_param)==ID_ANALYSIS_TYPE && HIWORD(w_param)==CBN_SELCHANGE) {
            update_form();
            return 0;
        }
        if (LOWORD(w_param)==ID_CALCULATE && HIWORD(w_param)==BN_CLICKED) {
            calculate_and_show(window);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window,message,w_param,l_param);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show_command) {
    using SetProcessDpiAwareFunction=BOOL (WINAPI*)();
    auto set_process_dpi_aware=reinterpret_cast<SetProcessDpiAwareFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDPIAware")
    );
    if (set_process_dpi_aware) {
        set_process_dpi_aware();
    }

    const wchar_t CLASS_NAME[]=L"NoChordNoLifeWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc=window_procedure;
    window_class.hInstance=instance;
    window_class.lpszClassName=CLASS_NAME;
    window_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    window_class.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);

    if (!RegisterClassW(&window_class)) {
        return 1;
    }

    HWND window=CreateWindowExW(
        0,CLASS_NAME,L"NoChordNoLife",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,575,430,
        nullptr,nullptr,instance,nullptr
    );
    if (!window) {
        return 1;
    }

    ShowWindow(window,show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}