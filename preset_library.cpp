#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "preset_library.hpp"
#include "midi_rhythm.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace ncnl {
namespace {
std::wstring absolute_path(const std::wstring& path) {
    DWORD length=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if (!length) { throw std::runtime_error("无法定位预设目录。"); }
    std::vector<wchar_t> result(length); GetFullPathNameW(path.c_str(),length,result.data(),nullptr);
    return result.data();
}
std::string utf8(const std::wstring& value) {
    int length=WideCharToMultiByte(CP_UTF8,0,value.data(),value.size(),nullptr,0,nullptr,nullptr);
    std::string result(length,'\0');
    if (length) { WideCharToMultiByte(CP_UTF8,0,value.data(),value.size(),&result[0],length,nullptr,nullptr); }
    return result;
}
std::wstring wide(const std::string& value) {
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),value.size(),nullptr,0);
    std::wstring result(length,L'\0');
    if (length) { MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),value.size(),&result[0],length); }
    return result;
}
std::vector<unsigned char> read_bytes(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) { return {}; }
    LARGE_INTEGER size{}; GetFileSizeEx(file,&size);
    if (size.QuadPart<0 || size.QuadPart>32*1024*1024) { CloseHandle(file); return {}; }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart)); DWORD read=0;
    BOOL ok=ReadFile(file,bytes.data(),bytes.size(),&read,nullptr); CloseHandle(file);
    if (!ok || read!=bytes.size()) { return {}; } return bytes;
}
void write_bytes(const std::wstring& path,const std::vector<unsigned char>& bytes,DWORD creation) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,creation,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) { throw std::runtime_error("无法保存：名称已存在或目录不可写，请换一个名称。"); }
    DWORD written=0; BOOL ok=WriteFile(file,bytes.data(),bytes.size(),&written,nullptr);
    BOOL flushed=FlushFileBuffers(file); CloseHandle(file);
    if (!ok || !flushed || written!=bytes.size()) {
        if (creation==CREATE_NEW) { DeleteFileW(path.c_str()); }
        throw std::runtime_error("写入预设失败，文件未完整保存。");
    }
}
bool midi_name(const std::wstring& name) {
    auto dot=name.find_last_of(L'.');
    return dot!=std::wstring::npos && (lstrcmpiW(name.c_str()+dot,L".mid")==0 || lstrcmpiW(name.c_str()+dot,L".midi")==0);
}
std::wstring valid_name(std::wstring name) {
    auto first=name.find_first_not_of(L" \t");
    if (first==std::wstring::npos) { throw std::runtime_error("请填写预设名称。"); }
    name=name.substr(first,name.find_last_not_of(L" \t")-first+1);
    if (midi_name(name)) { name=PresetStore::display_name(name); }
    if (name.empty() || name.size()>100 || name.back()==L'.' || name.back()==L' ' ||
        name.find_first_of(L"<>:\"/\\|?*\r\n\t")!=std::wstring::npos ||
        std::any_of(name.begin(),name.end(),[](wchar_t c){ return c<32; })) {
        throw std::runtime_error("名称无效：请勿使用路径、特殊符号或末尾的点，名称最长 100 字。");
    }
    std::wstring stem=name.substr(0,name.find(L'.'));
    if (!stem.empty()) { CharUpperBuffW(&stem[0],stem.size()); }
    if (stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" ||
        (stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT") && stem[3]>=L'1' && stem[3]<=L'9')) {
        throw std::runtime_error("这个名称是 Windows 保留名称，请换一个。");
    }
    return name;
}
}

PresetStore::PresetStore(const std::wstring& folder):directory(absolute_path(folder)) {
    DWORD attributes=GetFileAttributesW(directory.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(directory.c_str(),nullptr)) { throw std::runtime_error("无法创建 presents 预设目录。"); }
    } else if (!(attributes&FILE_ATTRIBUTE_DIRECTORY)) { throw std::runtime_error("预设目录不是文件夹。"); }
    refresh();
}
std::wstring PresetStore::display_name(const std::wstring& file) { return file.substr(0,file.find_last_of(L'.')); }
std::wstring PresetStore::path(std::size_t index) const { return directory+L"\\"+entries.at(index); }
void PresetStore::refresh() {
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW item{}; HANDLE search=FindFirstFileW((directory+L"\\*").c_str(),&item);
    if (search!=INVALID_HANDLE_VALUE) {
        do { if (!(item.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && midi_name(item.cFileName)) {
            found.push_back(item.cFileName);
        } } while (FindNextFileW(search,&item)); FindClose(search);
    }
    std::sort(found.begin(),found.end()); entries.clear();
    auto order=read_bytes(directory+L"\\order.txt");
    std::istringstream input(std::string(order.begin(),order.end())); std::string line;
    while (std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') { line.pop_back(); }
        auto name=wide(line);
        auto match=std::find(found.begin(),found.end(),name);
        if (match!=found.end()) { entries.push_back(*match); found.erase(match); }
    }
    entries.insert(entries.end(),found.begin(),found.end());
}
void PresetStore::persist_order() {
    std::string text;
    for (const auto& file:entries) { text+=utf8(file)+"\n"; }
    std::wstring temporary=directory+L"\\order.tmp";
    write_bytes(temporary,std::vector<unsigned char>(text.begin(),text.end()),CREATE_ALWAYS);
    if (!MoveFileExW(temporary.c_str(),(directory+L"\\order.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("预设文件已更新，但顺序信息无法保存。");
    }
}
std::size_t PresetStore::save(const std::wstring& name,const std::vector<unsigned char>& midi) {
    auto file=valid_name(name)+L".mid";
    parse_midi_rhythm(midi); // Never save an empty/corrupt MIDI that cannot be loaded back.
    write_bytes(directory+L"\\"+file,midi,CREATE_NEW);
    entries.push_back(file); persist_order(); return entries.size()-1;
}
void PresetStore::rename(std::size_t index,const std::wstring& name) {
    const auto& original=entries.at(index);
    std::wstring file=valid_name(name)+original.substr(original.find_last_of(L'.'));
    if (file==entries[index]) { return; }
    if (!MoveFileExW(path(index).c_str(),(directory+L"\\"+file).c_str(),0)) {
        throw std::runtime_error("无法重命名：名称已存在、文件被占用或目录不可写。");
    }
    entries[index]=file; persist_order();
}
void PresetStore::reorder(std::size_t from,std::size_t to) {
    if (from>=entries.size() || to>=entries.size()) { throw std::out_of_range("无效的预设位置。"); }
    if (from==to) { return; }
    auto file=entries[from]; entries.erase(entries.begin()+from); entries.insert(entries.begin()+to,file);
    persist_order();
}
void PresetStore::recycle(std::size_t index,HWND owner) {
    DWORD attributes=GetFileAttributesW(path(index).c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        throw std::runtime_error("这个预设已不存在或不是普通 MIDI 文件，请刷新列表。");
    }
    std::wstring file=path(index); file.push_back(L'\0'); file.push_back(L'\0');
    SHFILEOPSTRUCTW operation{}; operation.hwnd=owner; operation.wFunc=FO_DELETE;
    operation.pFrom=file.c_str(); operation.fFlags=FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_SILENT|FOF_NOERRORUI;
    if (SHFileOperationW(&operation)!=0 || operation.fAnyOperationsAborted) {
        throw std::runtime_error("无法删除这个预设，请确认文件未被其他程序占用。");
    }
    entries.erase(entries.begin()+index); persist_order();
}

namespace {
constexpr int LIST_TOP=150,ROW_HEIGHT=72,VISIBLE_ROWS=7;
constexpr UINT_PTR SCROLL_TIMER=71;
constexpr UINT_PTR TRANSITION_TIMER=72;
struct Library {
    std::unique_ptr<PresetStore> store;
    HWND window=nullptr,editor=nullptr,renamer=nullptr; HFONT edit_font=nullptr; WNDPROC edit_original=nullptr;
    int renaming=-1;
    ULONGLONG reveal_started=0,status_started=0,window_started=0;
    bool closing=false;
    std::wstring previous_status;
    Gdiplus::FontFamily* family=nullptr; std::wstring font_name;
    PresetLoad load; PresetSave save;
    double scale=1; bool resizing=false,dragging=false; POINT press={0,0};
    int selected=-1,scroll=0,drag_from=-1,drop=-1,erased_row=-1;
    std::wstring status=L"双击加载 · 拖动排序 · 按住右键经过预设可删除";
    HDC cache=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ old=nullptr; int width=0,height=0; bool dirty=true;
};
std::unique_ptr<Library> library;
MouseFeedback library_feedback;

void invalidate() { library->dirty=true; InvalidateRect(library->window,nullptr,FALSE); }
ULONGLONG transition_ms() {
    LARGE_INTEGER ticks{},frequency{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&frequency);
    return static_cast<ULONGLONG>(ticks.QuadPart*1000/frequency.QuadPart);
}
float progress(ULONGLONG start,int duration) {
    if (!start) { return 1; }
    float t=std::min(1.0f,static_cast<float>(transition_ms()-start)/duration);
    return t*t*(3-2*t);
}
void animate(bool rows=false) {
    if (rows) { library->reveal_started=transition_ms(); }
    SetTimer(library->window,TRANSITION_TIMER,16,nullptr); invalidate();
}
void notify(const std::wstring& value) {
    library->previous_status=library->status; library->status=value;
    library->status_started=transition_ms(); animate();
}
void position_renamer();
void finish_rename(bool commit) {
    int row=library->renaming;
    if (row<0) { return; }
    if (commit) {
        int length=GetWindowTextLengthW(library->renamer); std::vector<wchar_t> name(length+1);
        GetWindowTextW(library->renamer,name.data(),length+1);
        try { library->store->rename(row,name.data()); }
        catch (const std::exception& error) { notify(wide(error.what())); return; }
    }
    library->renaming=-1; ShowWindow(library->renamer,SW_HIDE);
    if (commit) { notify(L"名称已更新"); animate(true); }
    invalidate();
}
void begin_rename(int row) {
    if (row<0 || row>=static_cast<int>(library->store->files().size())) { return; }
    finish_rename(false); library->selected=row; library->renaming=row;
    library->scroll=std::max(0,std::min(library->scroll,row));
    if (row>=library->scroll+VISIBLE_ROWS) { library->scroll=row-VISIBLE_ROWS+1; }
    SetWindowTextW(library->renamer,PresetStore::display_name(library->store->files()[row]).c_str());
    position_renamer(); ShowWindow(library->renamer,SW_SHOW); SetFocus(library->renamer);
    SendMessageW(library->renamer,EM_SETSEL,0,-1); invalidate();
}
Gdiplus::PointF logical(LPARAM value) {
    return Gdiplus::PointF(static_cast<float>(GET_X_LPARAM(value)/library->scale),
        static_cast<float>(GET_Y_LPARAM(value)/library->scale));
}
bool in(const Gdiplus::PointF& point,float x,float y,float w,float h) {
    return Gdiplus::RectF(x,y,w,h).Contains(point);
}
int row_at(Gdiplus::PointF point) {
    if (!in(point,40,LIST_TOP,920,ROW_HEIGHT*VISIBLE_ROWS)) { return -1; }
    int index=library->scroll+static_cast<int>((point.Y-LIST_TOP)/ROW_HEIGHT);
    return index<static_cast<int>(library->store->files().size()) ? index : -1;
}
void clamp_scroll() {
    library->scroll=std::max(0,std::min(library->scroll,
        std::max(0,static_cast<int>(library->store->files().size())-VISIBLE_ROWS)));
}
void select(int row) {
    finish_rename(false);
    library->selected=row;
    invalidate();
}
std::wstring editor_text() {
    int length=GetWindowTextLengthW(library->editor); std::vector<wchar_t> value(length+1);
    GetWindowTextW(library->editor,value.data(),length+1); return value.data();
}
void action(int command) {
    finish_rename(false);
    try {
        if (command==0) {
            library->selected=static_cast<int>(library->store->save(editor_text(),library->save()));
            library->scroll=std::max(0,library->selected-VISIBLE_ROWS+1);
            notify(L"已保存为 MIDI 预设");
        } else if (command==1 && library->selected>=0) {
            notify(library->load(library->store->path(library->selected))
                ? L"已将预设加载到钢琴卷帘" : L"预设未加载，原有节奏保留");
        } else if (command==3) {
            library->store->refresh(); clamp_scroll(); select(-1); notify(L"已刷新预设列表");
        } else { notify(L"请先选择一个预设"); }
    } catch (const std::exception& error) {
        notify(wide(error.what()));
    }
    animate(true);
}
void erase_at(Gdiplus::PointF point) {
    int row=row_at(point);
    if (row<0) { library->erased_row=-1; return; }
    int spatial_row=static_cast<int>((point.Y-LIST_TOP)/ROW_HEIGHT);
    if (library->erased_row==spatial_row) { return; }
    library->erased_row=spatial_row;
    finish_rename(false);
    try {
        library->store->recycle(row,library->window);
        notify(L"预设已删除，可在回收站中恢复");
        clamp_scroll(); select(-1);
    } catch (const std::exception& error) { notify(wide(error.what())); }
    animate(true);
}
void rounded(Gdiplus::Graphics& graphics,Gdiplus::RectF box,float radius,Gdiplus::Color color) {
    Gdiplus::GraphicsPath path; float d=radius*2;
    path.AddArc(box.X,box.Y,d,d,180,90); path.AddArc(box.GetRight()-d,box.Y,d,d,270,90);
    path.AddArc(box.GetRight()-d,box.GetBottom()-d,d,d,0,90); path.AddArc(box.X,box.GetBottom()-d,d,d,90,90);
    path.CloseFigure(); Gdiplus::SolidBrush brush(color); graphics.FillPath(&brush,&path);
}
void text(Gdiplus::Graphics& graphics,const std::wstring& value,Gdiplus::RectF box,
    float size,Gdiplus::Color color=Gdiplus::Color(255,236,244,252),bool center=false) {
    Gdiplus::FontFamily fallback(L"Microsoft YaHei UI");
    auto family=library->family ? library->family : &fallback;
    Gdiplus::Font font(family,size,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color); Gdiplus::StringFormat format;
    format.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter); format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    graphics.DrawString(value.c_str(),-1,&font,box,&format,&brush);
}
void draw(Gdiplus::Graphics& graphics) {
    graphics.ScaleTransform(static_cast<float>(library->scale),static_cast<float>(library->scale));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::LinearGradientBrush background(Gdiplus::Point(0,0),Gdiplus::Point(1000,1000),
        Gdiplus::Color(255,20,29,47),Gdiplus::Color(255,47,35,65));
    graphics.FillRectangle(&background,0,0,1000,1000);
    text(graphics,L"节奏预设",{42,28,600,54},34);
    text(graphics,L"保留灵感的节奏，让下一次创作从这里开始。",{44,88,770,28},16,Gdiplus::Color(255,157,177,197));
    rounded(graphics,{832,44,120,42},14,Gdiplus::Color(255,50,64,88));
    text(graphics,L"刷新",{832,44,120,42},16,Gdiplus::Color(255,211,230,246),true);
    rounded(graphics,{30,135,940,545},22,Gdiplus::Color(150,13,21,36));
    if (library->store->files().empty()) {
        text(graphics,L"这里还没有预设",{140,295,720,70},28,Gdiplus::Color(255,204,225,239),true);
        text(graphics,L"在下方输入名称，保存当前的 MIDI 节奏。",{100,370,800,40},18,Gdiplus::Color(255,145,169,192),true);
    }
    for (int visible=0;visible<VISIBLE_ROWS;++visible) {
        int row=library->scroll+visible;
        if (row>=static_cast<int>(library->store->files().size())) { break; }
        float reveal=progress(library->reveal_started,220);
        float y=LIST_TOP+visible*ROW_HEIGHT+10*(1-reveal);
        bool selected=row==library->selected;
        rounded(graphics,{44,y+3,912,64},14,selected ? Gdiplus::Color(255,47,78,96) : Gdiplus::Color(255,34,43,61));
        rounded(graphics,{58,y+14,42,42},12,selected ? Gdiplus::Color(255,77,148,147) : Gdiplus::Color(255,55,69,90));
        text(graphics,L"♪",{58,y+14,42,42},25,Gdiplus::Color(255,178,235,230),true);
        if (row!=library->renaming) {
            text(graphics,PresetStore::display_name(library->store->files()[row]),{124,y+12,810,42},22,
                Gdiplus::Color(static_cast<BYTE>(255*reveal),236,244,252));
        }
        if (library->dragging && row==library->drop) {
            Gdiplus::Pen marker(Gdiplus::Color(255,156,247,217),3);
            graphics.DrawLine(&marker,55.0f,y+2,945.0f,y+2);
        }
    }
    std::wostringstream count; count<<library->store->files().size()<<L" 个预设  ·  presents";
    text(graphics,count.str(),{50,687,900,24},14,Gdiplus::Color(255,154,177,199));
    text(graphics,L"预设名称",{50,710,600,28},15,Gdiplus::Color(255,187,211,228));
    rounded(graphics,{674,746,276,50},16,Gdiplus::Color(255,69,149,123));
    text(graphics,L"＋ 保存当前节奏",{674,746,276,50},20,Gdiplus::Color(255,244,255,250),true);
    float status=progress(library->status_started,240);
    if (status<1) { text(graphics,library->previous_status,{50,897-8*status,900,38},15,
        Gdiplus::Color(static_cast<BYTE>(255*(1-status)),189,214,227)); }
    text(graphics,library->status,{50,897+8*(1-status),900,38},15,
        Gdiplus::Color(static_cast<BYTE>(255*status),189,214,227));
    text(graphics,L"双击空白处加载 · 双击名称 / F2 重命名 · 拖动排序 · 右键删除",{50,945,900,28},13,Gdiplus::Color(255,139,165,186));
}
void free_buffer() {
    if (library->cache) { SelectObject(library->cache,library->old); DeleteObject(library->bitmap); DeleteDC(library->cache); }
    library->cache=nullptr; library->bitmap=nullptr;
}
void position_renamer() {
    if (library->renaming<0) { return; }
    float y=LIST_TOP+(library->renaming-library->scroll)*ROW_HEIGHT+12;
    MoveWindow(library->renamer,static_cast<int>(124*library->scale),static_cast<int>(y*library->scale),
        static_cast<int>(810*library->scale),static_cast<int>(42*library->scale),TRUE);
}
void layout(HWND window) {
    RECT client{}; GetClientRect(window,&client); library->scale=client.right/1000.0;
    MoveWindow(library->editor,static_cast<int>(50*library->scale),static_cast<int>(746*library->scale),
        static_cast<int>(598*library->scale),static_cast<int>(50*library->scale),TRUE);
    int radius=static_cast<int>(24*library->scale);
    SetWindowRgn(library->editor,CreateRoundRectRgn(0,0,static_cast<int>(598*library->scale)+1,
        static_cast<int>(50*library->scale)+1,radius,radius),TRUE);
    SendMessageW(library->editor,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,
        MAKELPARAM(static_cast<int>(14*library->scale),static_cast<int>(14*library->scale)));
    HFONT old=library->edit_font;
    library->edit_font=CreateFontW(-static_cast<int>(22*library->scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,library->font_name.c_str());
    SendMessageW(library->editor,WM_SETFONT,reinterpret_cast<WPARAM>(library->edit_font),TRUE);
    SendMessageW(library->renamer,WM_SETFONT,reinterpret_cast<WPARAM>(library->edit_font),TRUE);
    position_renamer();
    if (old) { DeleteObject(old); } invalidate();
}
LRESULT CALLBACK edit_procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_CHAR && (w==VK_RETURN || w==VK_ESCAPE)) { return 0; }
    if (message==WM_KEYDOWN && w==VK_F2) { begin_rename(library->selected); return 0; }
    if (window==library->renamer) {
        if (message==WM_KEYDOWN && (w==VK_RETURN || w==VK_ESCAPE)) {
            finish_rename(w==VK_RETURN);
            if (library->renaming<0) { SetFocus(library->window); } return 0;
        }
        if (message==WM_KILLFOCUS) { finish_rename(true); }
    } else if (message==WM_KEYDOWN && w==VK_RETURN) { action(0); return 0; }
    return CallWindowProcW(library->edit_original,window,message,w,l);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    switch (message) {
    case WM_CREATE:
        library->window=window;
        library->editor=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
            0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(library->editor,EM_SETLIMITTEXT,100,0);
        library->edit_original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(library->editor,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_procedure)));
        library->renamer=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,
            0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(library->renamer,EM_SETLIMITTEXT,100,0);
        SetWindowLongPtrW(library->renamer,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_procedure));
        layout(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint); RECT client{}; GetClientRect(window,&client);
        if (!library->cache || library->width!=client.right || library->height!=client.bottom) {
            free_buffer(); library->cache=CreateCompatibleDC(dc); library->bitmap=CreateCompatibleBitmap(dc,client.right,client.bottom);
            library->old=SelectObject(library->cache,library->bitmap); library->width=client.right; library->height=client.bottom; library->dirty=true;
        }
        if (library->dirty) { Gdiplus::Graphics graphics(library->cache); draw(graphics); library->dirty=false; }
        BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,
            library->cache,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY); EndPaint(window,&paint); return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc=reinterpret_cast<HDC>(w); SetBkColor(dc,RGB(31,43,61)); SetTextColor(dc,RGB(230,245,250));
        static HBRUSH brush=CreateSolidBrush(RGB(31,43,61)); return reinterpret_cast<LRESULT>(brush);
    }
    case WM_SETCURSOR:
        if (LOWORD(l)==HTCLIENT || (LOWORD(l)>=HTTOP && LOWORD(l)<=HTBOTTOMRIGHT)) {
            AppCursor kind=(GetKeyState(VK_RBUTTON)&0x8000) ? AppCursor::unavailable :
                library->dragging || LOWORD(l)!=HTCLIENT ? AppCursor::vertical :
                (reinterpret_cast<HWND>(w)==library->editor || reinterpret_cast<HWND>(w)==library->renamer) ? AppCursor::text : AppCursor::link;
            SetCursor(library_feedback.cursor(kind)); return TRUE;
        } break;
    case WM_LBUTTONDBLCLK: {
        auto point=logical(l); int row=row_at(point);
        if (row>=0) {
            select(row);
            Gdiplus::Bitmap measure(1,1); Gdiplus::Graphics graphics(&measure);
            Gdiplus::FontFamily fallback(L"Microsoft YaHei UI");
            Gdiplus::Font font(library->family ? library->family : &fallback,22,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            auto name=PresetStore::display_name(library->store->files()[row]); Gdiplus::RectF bounds;
            graphics.MeasureString(name.c_str(),-1,&font,Gdiplus::PointF(0,0),&bounds);
            float y=LIST_TOP+(row-library->scroll)*ROW_HEIGHT;
            if (in(point,124,y+12,std::min(810.0f,std::max(50.0f,bounds.Width)),42)) { begin_rename(row); }
            else { action(1); }
        } return 0;
    }
    case WM_KEYDOWN: if (w==VK_F2) { begin_rename(library->selected); return 0; } break;
    case WM_LBUTTONDOWN: {
        auto point=logical(l);
        if (in(point,674,746,276,50)) { action(0); return 0; }
        if (in(point,832,44,120,42)) { action(3); return 0; }
        int row=row_at(point); if (row>=0) {
            select(row);
            library->drag_from=row; library->drop=row; library->press={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            SetCapture(window); SetTimer(window,SCROLL_TIMER,100,nullptr);
        } return 0;
    }
    case WM_MOUSEMOVE: {
        auto point=logical(l);
        if (w&MK_RBUTTON) { SetCursor(library_feedback.cursor(AppCursor::unavailable)); erase_at(point); return 0; }
        if (library->drag_from>=0 && (w&MK_LBUTTON)) {
            if (std::abs(GET_Y_LPARAM(l)-library->press.y)>GetSystemMetrics(SM_CYDRAG)) { library->dragging=true; }
            if (library->dragging) {
                int target=library->scroll+static_cast<int>((point.Y-LIST_TOP)/ROW_HEIGHT);
                library->drop=std::max(0,std::min(target,static_cast<int>(library->store->files().size())-1));
                SetCursor(library_feedback.cursor(AppCursor::vertical)); invalidate();
            }
        } return 0;
    }
    case WM_TIMER:
        if (w==TRANSITION_TIMER) {
            float window_progress=progress(library->window_started,library->closing ? 140 : 200);
            SetLayeredWindowAttributes(window,0,static_cast<BYTE>(255*(library->closing ? 1-window_progress : window_progress)),LWA_ALPHA);
            if (library->closing && window_progress>=1) { DestroyWindow(window); return 0; }
            if (window_progress>=1 && progress(library->reveal_started,220)>=1 && progress(library->status_started,240)>=1) {
                KillTimer(window,TRANSITION_TIMER);
            }
            if (!library->resizing && !IsIconic(window)) { invalidate(); }
            return 0;
        }
        if (w==SCROLL_TIMER && library->dragging) {
            POINT point{}; GetCursorPos(&point); ScreenToClient(window,&point);
            float y=static_cast<float>(point.y/library->scale);
            if (y<LIST_TOP+24) { --library->scroll; }
            if (y>LIST_TOP+ROW_HEIGHT*VISIBLE_ROWS-24) { ++library->scroll; }
            clamp_scroll(); invalidate();
            SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y));
        } return 0;
    case WM_LBUTTONUP: {
        int from=library->drag_from,to=library->drop; bool drag=library->dragging;
        library->drag_from=-1; library->dragging=false; KillTimer(window,SCROLL_TIMER);
        if (GetCapture()==window) { ReleaseCapture(); }
        if (drag && from>=0 && to>=0) {
            try { library->store->reorder(from,to); select(to); notify(L"预设顺序已保存"); animate(true); }
            catch (const std::exception& error) { notify(wide(error.what())); }
        }
        SetCursor(library_feedback.cursor(AppCursor::link)); invalidate(); return 0;
    }
    case WM_RBUTTONDOWN:
        library->dragging=false; library->drag_from=-1; KillTimer(window,SCROLL_TIMER);
        SetCursor(library_feedback.cursor(AppCursor::unavailable));
        library->erased_row=-1; SetCapture(window); erase_at(logical(l)); return 0;
    case WM_RBUTTONUP: library->erased_row=-1; if (GetCapture()==window) { ReleaseCapture(); } return 0;
    case WM_CONTEXTMENU: return 0;
    case WM_CAPTURECHANGED:
        library->dragging=false; library->drag_from=-1; KillTimer(window,SCROLL_TIMER); library_feedback.break_trail(); invalidate(); return 0;
    case WM_MOUSEWHEEL: finish_rename(true); if (library->renaming>=0) { return 0; }
        library->scroll-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*2; clamp_scroll(); animate(true); return 0;
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(l);
        RECT minimum={0,0,600,600};
        AdjustWindowRectEx(&minimum,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)));
        int fw=minimum.right-minimum.left-600,fh=minimum.bottom-minimum.top-600;
        info->ptMinTrackSize={600+fw,600+fh};
        int maximum=std::max<int>(600,std::min(info->ptMaxTrackSize.x-fw,info->ptMaxTrackSize.y-fh));
        info->ptMaxTrackSize={maximum+fw,maximum+fh};
        return 0;
    }
    case WM_SIZING: {
        RECT frame{},client{}; GetWindowRect(window,&frame); GetClientRect(window,&client);
        int fw=frame.right-frame.left-client.right,fh=frame.bottom-frame.top-client.bottom;
        auto proposed=reinterpret_cast<RECT*>(l);
        int side=std::max(600,static_cast<int>((w==WMSZ_TOP || w==WMSZ_BOTTOM) ? proposed->bottom-proposed->top-fh : proposed->right-proposed->left-fw));
        if (w==WMSZ_LEFT || w==WMSZ_TOPLEFT || w==WMSZ_BOTTOMLEFT) { proposed->left=proposed->right-side-fw; }
        else { proposed->right=proposed->left+side+fw; }
        if (w==WMSZ_TOP || w==WMSZ_TOPLEFT || w==WMSZ_TOPRIGHT) { proposed->top=proposed->bottom-side-fh; }
        else { proposed->bottom=proposed->top+side+fh; } return TRUE;
    }
    case WM_ENTERSIZEMOVE: library->resizing=true; library_feedback.clear(); return 0;
    case WM_EXITSIZEMOVE: library->resizing=false; layout(window); return 0;
    case WM_SIZE:
        if (w==SIZE_MINIMIZED) { library_feedback.clear(); }
        else if (!library->resizing && library->editor) { layout(window); }
        return 0;
    case WM_CLOSE:
        finish_rename(true); library->closing=true; library->window_started=transition_ms();
        EnableWindow(window,FALSE);
        library_feedback.clear(); SetTimer(window,TRANSITION_TIMER,16,nullptr); return 0;
    case WM_DESTROY:
        KillTimer(window,SCROLL_TIMER); KillTimer(window,TRANSITION_TIMER); library_feedback.shutdown(); free_buffer();
        if (library->edit_font) { DeleteObject(library->edit_font); library->edit_font=nullptr; }
        library->window=nullptr; return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
}

HWND preset_library_window() { return library ? library->window : nullptr; }
MouseFeedback& preset_library_feedback() { return library_feedback; }
void close_preset_library() { if (preset_library_window()) { DestroyWindow(preset_library_window()); } }
void open_preset_library(HWND owner,const std::wstring& directory,const std::wstring& cursors,
    Gdiplus::FontFamily* font,const std::wstring& font_name,PresetLoad load,PresetSave save) {
    if (preset_library_window()) { ShowWindow(preset_library_window(),SW_RESTORE); SetForegroundWindow(preset_library_window()); return; }
    try {
        library.reset(new Library); library->store.reset(new PresetStore(directory));
        library->family=font; library->font_name=font_name; library->load=std::move(load); library->save=std::move(save);
        WNDCLASSW type{}; type.style=CS_DBLCLKS; type.lpfnWndProc=procedure; type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"NoChordNoLifePresetLibrary"; RegisterClassW(&type);
        DWORD style=WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX; RECT size={0,0,1000,1000}; AdjustWindowRect(&size,style,FALSE);
        HWND window=CreateWindowExW(WS_EX_LAYERED,type.lpszClassName,L"NoChordNoLife · 节奏预设",style,CW_USEDEFAULT,CW_USEDEFAULT,
            size.right-size.left,size.bottom-size.top,owner,nullptr,type.hInstance,nullptr);
        if (!window) { throw std::runtime_error("无法打开预设管理界面。"); }
        library_feedback.initialize(window,cursors);
        library->window_started=transition_ms(); library->reveal_started=library->window_started;
        SetLayeredWindowAttributes(window,0,0,LWA_ALPHA); SetTimer(window,TRANSITION_TIMER,16,nullptr);
        ShowWindow(window,SW_SHOW); UpdateWindow(window);
    } catch (const std::exception& error) { MessageBoxW(owner,wide(error.what()).c_str(),L"预设管理",MB_OK|MB_ICONERROR); }
}
}