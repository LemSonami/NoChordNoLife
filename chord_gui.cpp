#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "chord_algorithms.hpp"
#include "chord_generator.hpp"
#include "progression_config.hpp"

int BPM=120;

namespace {

constexpr int ID_GENERATE=1003;
constexpr int ID_SETTINGS=1004;
constexpr int ID_PRESET_ACTION=1005;
constexpr int ID_PRESET_BASE=1100;
constexpr int ID_SLOT_BASE=1200;
constexpr int ID_CHORD_HEADER_BASE=1400;
constexpr UINT WM_CLEAR_CHORD_HOVER=WM_APP+1;

constexpr int DESIGN_WIDTH=1000;
constexpr int DESIGN_HEIGHT=1000;
constexpr int MIN_CLIENT_SIZE=600;
constexpr int SETTINGS_DESIGN_SIZE=800;
constexpr int SLOT_Y=735;
constexpr int SLOT_SIZE=125;
const std::array<int,4> SLOT_X={{100,325,550,775}};

enum class FontKind {
    title,
    normal,
    card,
    hint,
};

struct ControlLayout {
    HWND window;
    int x;
    int y;
    int width;
    int height;
    FontKind font_kind;
    bool combo_box;
};

std::vector<ControlLayout> controls;

HWND main_window=nullptr;
HWND configuration_window=nullptr;
HWND settings_button=nullptr;
HWND generate_button=nullptr;
HWND chord_editor=nullptr;
WNDPROC chord_editor_procedure_original=nullptr;
std::array<HWND,5> preset_buttons={{nullptr,nullptr,nullptr,nullptr,nullptr}};
std::array<HWND,4> slot_buttons={{nullptr,nullptr,nullptr,nullptr}};
std::array<HWND,4> preset_labels={{nullptr,nullptr,nullptr,nullptr}};
std::array<HWND,4> chord_header_targets={{nullptr,nullptr,nullptr,nullptr}};
std::array<int,4> slot_presets={{2,2,2,2}};
std::array<WNDPROC,5> preset_button_procedures={{nullptr,nullptr,nullptr,nullptr,nullptr}};
int active_slot=0;
int dragged_preset=-1;
int drag_hover_slot=-1;
bool preset_dragging=false;
POINT preset_drag_start={0,0};

HFONT title_font=nullptr;
HFONT normal_font=nullptr;
HFONT card_font=nullptr;
HFONT hint_font=nullptr;
double current_scale=1.0;
bool interactive_resize=false;
int applied_client_width=-1;
int applied_client_height=-1;
HDC main_background_dc=nullptr;
HBITMAP main_background_bitmap=nullptr;
HGDIOBJ main_background_old_bitmap=nullptr;
int main_background_width=0;
int main_background_height=0;
bool main_background_dirty=true;

ULONG_PTR gdiplus_token=0;
std::unique_ptr<Gdiplus::Image> background_image;
std::wstring background_path;
std::array<std::unique_ptr<Gdiplus::Image>,5> emotion_images;
std::unique_ptr<Gdiplus::Image> arrow_image;
std::unique_ptr<Gdiplus::Image> pentagon_image;
std::string progression_config_path;
bool configuration_interactive_resize=false;
int configuration_tab=0;
int dragged_weight_axis=-1;
std::string current_mode="C Ionian";
ncnl::GeneratedProgression displayed_progression{};
int editing_chord=-1;
int dragged_midi_position=-1;
int dragged_midi_pitch=-1;
HANDLE midi_playback_thread=nullptr;
HANDLE midi_stop_event=nullptr;

std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int size=MultiByteToWideChar(
        CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        nullptr,0
    );
    if (size<=0) {
        return L"无法显示文本";
    }
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    MultiByteToWideChar(
        CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),
        &result[0],size
    );
    return result;
}

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

std::wstring get_window_text(HWND window) {
    int length=GetWindowTextLengthW(window);
    std::wstring result(static_cast<std::size_t>(length+1),L'\0');
    if (length>0) {
        GetWindowTextW(window,&result[0],length+1);
    }
    result.resize(static_cast<std::size_t>(length));
    return result;
}

void add_rounded_rectangle(
    Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& bounds,
    float radius
) {
    float diameter=std::min(
        radius*2.0f,std::min(bounds.Width,bounds.Height)
    );
    Gdiplus::RectF arc(bounds.X,bounds.Y,diameter,diameter);
    path.AddArc(arc,180.0f,90.0f);
    arc.X=bounds.X+bounds.Width-diameter;
    path.AddArc(arc,270.0f,90.0f);
    arc.Y=bounds.Y+bounds.Height-diameter;
    path.AddArc(arc,0.0f,90.0f);
    arc.X=bounds.X;
    path.AddArc(arc,90.0f,90.0f);
    path.CloseFigure();
}

std::wstring executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH,L'\0');
    for (;;) {
        DWORD length=GetModuleFileNameW(
            nullptr,buffer.data(),static_cast<DWORD>(buffer.size())
        );
        if (length==0) {
            return L".";
        }
        if (length<buffer.size()-1) {
            std::wstring path(buffer.data(),length);
            std::size_t separator=path.find_last_of(L"\\/");
            return separator==std::wstring::npos ? L"." : path.substr(0,separator);
        }
        buffer.resize(buffer.size()*2,L'\0');
    }
}

std::wstring full_path(const std::wstring& path) {
    DWORD length=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if (length==0) {
        return path;
    }
    std::vector<wchar_t> buffer(length,L'\0');
    GetFullPathNameW(path.c_str(),length,buffer.data(),nullptr);
    return std::wstring(buffer.data());
}

bool same_path(const std::wstring& left,const std::wstring& right) {
    std::wstring normalized_left=full_path(left);
    std::wstring normalized_right=full_path(right);
    return lstrcmpiW(normalized_left.c_str(),normalized_right.c_str())==0;
}

bool load_background(const std::wstring& path) {
    std::unique_ptr<Gdiplus::Image> image(new Gdiplus::Image(path.c_str()));
    if (image->GetLastStatus()!=Gdiplus::Ok || image->GetWidth()==0 || image->GetHeight()==0) {
        return false;
    }
    background_image=std::move(image);
    main_background_dirty=true;
    if (main_window) {
        InvalidateRect(main_window,nullptr,FALSE);
    }
    return true;
}

std::unique_ptr<Gdiplus::Image> load_png(const std::wstring& path) {
    std::unique_ptr<Gdiplus::Image> image(new Gdiplus::Image(path.c_str()));
    if (image->GetLastStatus()!=Gdiplus::Ok ||
        image->GetWidth()==0 || image->GetHeight()==0) {
        return {};
    }
    return image;
}

void load_interface_images() {
    std::wstring root=executable_directory()+L"\\assets";
    for (int preset=0;preset<5;++preset) {
        std::wostringstream path;
        path<<root<<L"\\chord_emotion\\"<<preset+1<<L".png";
        emotion_images[static_cast<std::size_t>(preset)]=load_png(path.str());
    }
    arrow_image=load_png(root+L"\\arrow.png");
    pentagon_image=load_png(root+L"\\res\\penta_dim.png");
}

void choose_background(HWND owner) {
    wchar_t selected_file[MAX_PATH]={};
    const wchar_t filter[]=L"PNG 图片 (*.png)\0*.png\0所有文件 (*.*)\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);
    dialog.hwndOwner=owner;
    dialog.lpstrFilter=filter;
    dialog.lpstrFile=selected_file;
    dialog.nMaxFile=MAX_PATH;
    dialog.lpstrDefExt=L"png";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    std::unique_ptr<Gdiplus::Image> candidate(new Gdiplus::Image(selected_file));
    if (candidate->GetLastStatus()!=Gdiplus::Ok ||
        candidate->GetWidth()==0 || candidate->GetHeight()==0) {
        MessageBoxW(owner,L"所选文件不是可读取的 PNG 图片。",L"背景设置失败",MB_OK|MB_ICONERROR);
        return;
    }

    std::wstring assets_directory=executable_directory()+L"\\assets";
    if (!CreateDirectoryW(assets_directory.c_str(),nullptr) &&
        GetLastError()!=ERROR_ALREADY_EXISTS) {
        MessageBoxW(owner,L"无法创建 assets 文件夹。",L"背景设置失败",MB_OK|MB_ICONERROR);
        return;
    }

    if (!same_path(selected_file,background_path)) {
        background_image.reset();
        if (!CopyFileW(selected_file,background_path.c_str(),FALSE)) {
            load_background(background_path);
            MessageBoxW(owner,L"无法把图片复制到 assets/bg.png。",L"背景设置失败",MB_OK|MB_ICONERROR);
            return;
        }
    }

    if (!load_background(background_path)) {
        MessageBoxW(owner,L"背景图片缓存成功，但重新读取失败。",L"背景设置失败",MB_OK|MB_ICONERROR);
        return;
    }
    MessageBoxW(owner,L"背景图片已保存到 assets/bg.png。",L"配置",MB_OK|MB_ICONINFORMATION);
}

HFONT create_scaled_font(int logical_height,int weight,double scale) {
    int height=-std::max(9,static_cast<int>(std::lround(logical_height*scale)));
    return CreateFontW(
        height,0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei UI"
    );
}

void rebuild_fonts(double scale) {
    HFONT new_title=create_scaled_font(30,FW_BOLD,scale);
    HFONT new_normal=create_scaled_font(19,FW_NORMAL,scale);
    HFONT new_card=create_scaled_font(17,FW_NORMAL,scale);
    HFONT new_hint=create_scaled_font(15,FW_NORMAL,scale);

    for (const auto& control:controls) {
        HFONT font=new_normal;
        if (control.font_kind==FontKind::title) {
            font=new_title;
        }
        else if (control.font_kind==FontKind::card) {
            font=new_card;
        }
        else if (control.font_kind==FontKind::hint) {
            font=new_hint;
        }
        SendMessageW(control.window,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }

    if (title_font) {
        DeleteObject(title_font);
    }
    if (normal_font) {
        DeleteObject(normal_font);
    }
    if (card_font) {
        DeleteObject(card_font);
    }
    if (hint_font) {
        DeleteObject(hint_font);
    }
    title_font=new_title;
    normal_font=new_normal;
    card_font=new_card;
    hint_font=new_hint;
}

void apply_layout(HWND window) {
    RECT client{};
    GetClientRect(window,&client);
    int client_width=client.right-client.left;
    int client_height=client.bottom-client.top;
    if (client_width<=0 || client_height<=0 ||
        (client_width==applied_client_width && client_height==applied_client_height)) {
        return;
    }
    double scale_x=client_width/static_cast<double>(DESIGN_WIDTH);
    double scale_y=client_height/static_cast<double>(DESIGN_HEIGHT);
    current_scale=std::max(0.25,std::min(scale_x,scale_y));
    int offset_x=static_cast<int>(std::lround((client_width-DESIGN_WIDTH*current_scale)/2.0));
    int offset_y=static_cast<int>(std::lround((client_height-DESIGN_HEIGHT*current_scale)/2.0));

    rebuild_fonts(current_scale);

    HDWP positions=BeginDeferWindowPos(static_cast<int>(controls.size()));
    for (const auto& control:controls) {
        int x=offset_x+static_cast<int>(std::lround(control.x*current_scale));
        int y=offset_y+static_cast<int>(std::lround(control.y*current_scale));
        int width=std::max(1,static_cast<int>(std::lround(control.width*current_scale)));
        int visible_height=std::max(1,static_cast<int>(std::lround(control.height*current_scale)));
        int window_height=control.combo_box
            ? std::max(visible_height,static_cast<int>(std::lround(260*current_scale)))
            : visible_height;
        positions=DeferWindowPos(
            positions,control.window,nullptr,x,y,width,window_height,
            SWP_NOZORDER|SWP_NOACTIVATE
        );
    }
    if (positions) {
        EndDeferWindowPos(positions);
    }
    for (const auto& control:controls) {
        wchar_t class_name[32]={};
        GetClassNameW(control.window,class_name,32);
        if (lstrcmpiW(class_name,L"BUTTON")==0) {
            RECT bounds{};
            GetClientRect(control.window,&bounds);
            int radius=std::max(14,static_cast<int>(std::lround(32*current_scale)));
            SetWindowRgn(
                control.window,
                CreateRoundRectRgn(
                    0,0,bounds.right+1,bounds.bottom+1,radius,radius
                ),
                TRUE
            );
        }
    }
    applied_client_width=client_width;
    applied_client_height=client_height;
    InvalidateRect(window,nullptr,FALSE);
}

void redraw_transparent_controls(const HWND* windows,std::size_t count) {
    RECT dirty{};
    bool has_dirty_region=false;
    for (std::size_t index=0;index<count;++index) {
        if (!windows[index]) {
            continue;
        }
        RECT bounds{};
        GetWindowRect(windows[index],&bounds);
        POINT corners[2]={{bounds.left,bounds.top},{bounds.right,bounds.bottom}};
        MapWindowPoints(HWND_DESKTOP,main_window,corners,2);
        RECT mapped={corners[0].x,corners[0].y,corners[1].x,corners[1].y};
        if (!has_dirty_region) {
            dirty=mapped;
            has_dirty_region=true;
        }
        else {
            RECT combined{};
            UnionRect(&combined,&dirty,&mapped);
            dirty=combined;
        }
    }
    if (has_dirty_region) {
        RedrawWindow(
            main_window,&dirty,nullptr,
            RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN
        );
    }
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
    int id,
    FontKind font_kind=FontKind::normal,
    bool combo_box=false
) {
    HWND control=CreateWindowExW(
        0,class_name,text,style,
        x,y,width,combo_box ? 260 : height,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr
    );
    controls.push_back({control,x,y,width,height,font_kind,combo_box});
    return control;
}

void update_slot_ui() {
    std::array<HWND,4> changed_controls;
    for (int position=0;position<4;++position) {
        std::wostringstream preset;
        preset<<L"和弦 "<<position+1<<L" · "
              <<utf8_to_wide(ncnl::emotion_preset_label(slot_presets[position]));
        if (position==active_slot) {
            preset<<L"（当前）";
        }
        SetWindowTextW(preset_labels[position],preset.str().c_str());
        changed_controls[static_cast<std::size_t>(position)]=preset_labels[position];
        InvalidateRect(slot_buttons[position],nullptr,FALSE);
    }
    redraw_transparent_controls(changed_controls.data(),changed_controls.size());
    for (HWND button:preset_buttons) {
        InvalidateRect(button,nullptr,FALSE);
    }
}

int slot_at_client_point(HWND window,POINT point) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    double offset_x=(width-DESIGN_WIDTH*scale)/2.0;
    double offset_y=(height-DESIGN_HEIGHT*scale)/2.0;
    double logical_x=(point.x-offset_x)/scale;
    double logical_y=(point.y-offset_y)/scale;
    if (logical_y<SLOT_Y || logical_y>SLOT_Y+SLOT_SIZE) {
        return -1;
    }
    for (int position=0;position<4;++position) {
        double left=SLOT_X[static_cast<std::size_t>(position)];
        if (logical_x>=left && logical_x<=left+SLOT_SIZE) {
            return position;
        }
    }
    return -1;
}

bool piano_logical_point(HWND window,POINT point,double& logical_x,double& logical_y) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    double offset_x=(width-DESIGN_WIDTH*scale)/2.0;
    double offset_y=(height-DESIGN_HEIGHT*scale)/2.0;
    logical_x=(point.x-offset_x)/scale;
    logical_y=(point.y-offset_y)/scale;
    return true;
}

int piano_position_at_client_point(HWND window,POINT point,bool header_only) {
    double logical_x=0.0;
    double logical_y=0.0;
    piano_logical_point(window,point,logical_x,logical_y);
    double bottom=header_only ? 347.0 : 645.0;
    if (logical_y<295.0 || logical_y>bottom ||
        logical_x<132.0 || logical_x>950.0) {
        return -1;
    }
    int position=static_cast<int>((logical_x-132.0)/((950.0-132.0)/4.0));
    return std::max(0,std::min(3,position));
}

int piano_header_at_client_point(HWND window,POINT point) {
    return piano_position_at_client_point(window,point,true);
}

bool piano_note_at_client_point(HWND window,POINT point,int& position,int& pitch) {
    double logical_x=0.0;
    double logical_y=0.0;
    piano_logical_point(window,point,logical_x,logical_y);
    const double grid_left=132.0;
    const double grid_top=347.0;
    const double grid_bottom=645.0;
    const double section_width=(950.0-grid_left)/4.0;
    if (logical_x<grid_left || logical_x>950.0 ||
        logical_y<grid_top || logical_y>=grid_bottom) {
        return false;
    }
    position=std::max(0,std::min(
        3,static_cast<int>((logical_x-grid_left)/section_width)
    ));
    double within_section=logical_x-(grid_left+position*section_width);
    if (within_section<10.0 || within_section>section_width-10.0) {
        return false;
    }
    int lane=std::max(0,std::min(
        11,static_cast<int>((logical_y-grid_top)/((grid_bottom-grid_top)/12.0))
    ));
    pitch=11-lane;
    return true;
}

RECT slot_client_rect(HWND window,int position) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    double offset_x=(width-DESIGN_WIDTH*scale)/2.0;
    double offset_y=(height-DESIGN_HEIGHT*scale)/2.0;
    RECT result={
        static_cast<LONG>(std::lround(offset_x+SLOT_X[static_cast<std::size_t>(position)]*scale)),
        static_cast<LONG>(std::lround(offset_y+SLOT_Y*scale)),
        static_cast<LONG>(std::lround(offset_x+(SLOT_X[static_cast<std::size_t>(position)]+SLOT_SIZE)*scale)),
        static_cast<LONG>(std::lround(offset_y+(SLOT_Y+SLOT_SIZE)*scale)),
    };
    return result;
}

void toggle_drag_highlight(int position) {
    if (position<0 || position>=4 || !main_window) {
        return;
    }
    RECT bounds=slot_client_rect(main_window,position);
    InflateRect(&bounds,-2,-2);
    HDC dc=GetDC(main_window);
    DrawFocusRect(dc,&bounds);
    ReleaseDC(main_window,dc);
}

void set_drag_hover_slot(int position) {
    if (position==drag_hover_slot) {
        return;
    }
    toggle_drag_highlight(drag_hover_slot);
    drag_hover_slot=position;
    toggle_drag_highlight(drag_hover_slot);
}

LRESULT CALLBACK preset_button_procedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    int preset=GetDlgCtrlID(window)-ID_PRESET_BASE;
    WNDPROC original=(preset>=0 && preset<5)
        ? preset_button_procedures[static_cast<std::size_t>(preset)]
        : nullptr;

    if (message==WM_LBUTTONDOWN && preset>=0 && preset<5) {
        dragged_preset=preset;
        preset_dragging=false;
        preset_drag_start={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        ClientToScreen(window,&preset_drag_start);
        SetCapture(window);
        SetFocus(window);
        return 0;
    }
    if (message==WM_MOUSEMOVE && dragged_preset==preset && GetCapture()==window) {
        POINT screen={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        ClientToScreen(window,&screen);
        if (!preset_dragging &&
            (std::abs(screen.x-preset_drag_start.x)>=GetSystemMetrics(SM_CXDRAG) ||
             std::abs(screen.y-preset_drag_start.y)>=GetSystemMetrics(SM_CYDRAG))) {
            preset_dragging=true;
        }
        if (preset_dragging) {
            POINT client_point=screen;
            ScreenToClient(main_window,&client_point);
            int hover=slot_at_client_point(main_window,client_point);
            set_drag_hover_slot(hover);
            SetCursor(LoadCursorW(nullptr,IDC_HAND));
        }
        return 0;
    }
    if (message==WM_LBUTTONUP && dragged_preset==preset && GetCapture()==window) {
        POINT screen={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        ClientToScreen(window,&screen);
        POINT client_point=screen;
        ScreenToClient(main_window,&client_point);
        int target=slot_at_client_point(main_window,client_point);
        bool was_dragging=preset_dragging;
        int selected_preset=dragged_preset;
        set_drag_hover_slot(-1);
        ReleaseCapture();
        dragged_preset=-1;
        preset_dragging=false;
        if (was_dragging) {
            if (target>=0) {
                active_slot=target;
                slot_presets[target]=selected_preset;
                update_slot_ui();
            }
        }
        else {
            SendMessageW(
                main_window,WM_COMMAND,
                MAKEWPARAM(ID_PRESET_BASE+selected_preset,BN_CLICKED),
                reinterpret_cast<LPARAM>(window)
            );
        }
        return 0;
    }
    if (message==WM_CAPTURECHANGED && dragged_preset==preset) {
        set_drag_hover_slot(-1);
        dragged_preset=-1;
        preset_dragging=false;
    }
    return original
        ? CallWindowProcW(original,window,message,w_param,l_param)
        : DefWindowProcW(window,message,w_param,l_param);
}

struct MidiPlaybackData {
    HMIDIOUT output;
    HANDLE stop_event;
    int bpm;
    std::array<ncnl::Chord,4> chords;
};

void send_midi_note(HMIDIOUT output,int note,int velocity,bool note_on) {
    DWORD status=note_on ? 0x90u : 0x80u;
    DWORD message=status |
        (static_cast<DWORD>(note&0x7F)<<8) |
        (static_cast<DWORD>(velocity&0x7F)<<16);
    midiOutShortMsg(output,message);
}

DWORD WINAPI midi_playback_procedure(LPVOID parameter) {
    std::unique_ptr<MidiPlaybackData> data(
        static_cast<MidiPlaybackData*>(parameter)
    );
    // General MIDI program 0：Acoustic Grand Piano。
    midiOutShortMsg(data->output,0xC0u);
    int beat_ms=std::max(60,60000/std::max(1,data->bpm));
    int sounding_ms=std::max(40,beat_ms*9/10);

    bool stopping=false;
    while (!stopping) {
        for (int position=0;position<4 && !stopping;++position) {
            for (int pitch_class:data->chords[position]) {
                send_midi_note(data->output,60+pitch_class,92,true);
            }
            stopping=WaitForSingleObject(
                data->stop_event,static_cast<DWORD>(sounding_ms)
            )==WAIT_OBJECT_0;
            for (int pitch_class:data->chords[position]) {
                send_midi_note(data->output,60+pitch_class,0,false);
            }
            if (!stopping) {
                stopping=WaitForSingleObject(
                    data->stop_event,static_cast<DWORD>(beat_ms-sounding_ms)
                )==WAIT_OBJECT_0;
            }
        }
    }
    midiOutReset(data->output);
    midiOutClose(data->output);
    return 0;
}

void stop_midi_playback() {
    if (!midi_playback_thread) {
        return;
    }
    SetEvent(midi_stop_event);
    if (WaitForSingleObject(midi_playback_thread,5000)==WAIT_OBJECT_0) {
        CloseHandle(midi_playback_thread);
        CloseHandle(midi_stop_event);
        midi_playback_thread=nullptr;
        midi_stop_event=nullptr;
    }
}

void toggle_midi_playback(HWND owner) {
    if (midi_playback_thread) {
        stop_midi_playback();
        return;
    }

    std::unique_ptr<MidiPlaybackData> data(new MidiPlaybackData{});
    for (int position=0;position<4;++position) {
        const std::string& notes=displayed_progression.chords[position].notes;
        if (notes.empty()) {
            MessageBoxW(
                owner,L"请先生成或填写完整的四个和弦。",
                L"无法播放",MB_OK|MB_ICONINFORMATION
            );
            return;
        }
        data->chords[position]=ncnl::parse_chord(notes);
    }

    MMRESULT opened=midiOutOpen(
        &data->output,MIDI_MAPPER,0,0,CALLBACK_NULL
    );
    if (opened!=MMSYSERR_NOERROR) {
        MessageBoxW(
            owner,L"无法打开 Windows MIDI 播放设备。",
            L"无法播放",MB_OK|MB_ICONERROR
        );
        return;
    }

    midi_stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!midi_stop_event) {
        midiOutClose(data->output);
        MessageBoxW(owner,L"无法创建播放事件。",L"无法播放",MB_OK|MB_ICONERROR);
        return;
    }
    data->stop_event=midi_stop_event;
    data->bpm=BPM;
    midi_playback_thread=CreateThread(
        nullptr,0,midi_playback_procedure,data.get(),0,nullptr
    );
    if (!midi_playback_thread) {
        CloseHandle(midi_stop_event);
        midi_stop_event=nullptr;
        midiOutClose(data->output);
        MessageBoxW(owner,L"无法创建播放线程。",L"无法播放",MB_OK|MB_ICONERROR);
        return;
    }
    data.release();
}

void generate_and_show(HWND owner) {
    try {
        stop_midi_playback();
        std::array<ncnl::ChordConstraint,4> constraints;
        bool all_chords_filled=true;
        for (const auto& chord:displayed_progression.chords) {
            all_chords_filled=all_chords_filled && !chord.notes.empty();
        }
        for (int position=0;position<4;++position) {
            constraints[position].emotion_preset=slot_presets[position];
            // 只重新生成已清空的位置；如果四个位置都有和弦，则把这次操作
            // 解释为“全部重新生成”。
            constraints[position].fixed_notes=all_chords_filled
                ? ""
                : displayed_progression.chords[position].notes;
        }

        displayed_progression=ncnl::generate_progression(
            current_mode,constraints
        );
        main_background_dirty=true;
        RedrawWindow(
            main_window,nullptr,nullptr,
            RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN
        );
    }
    catch (const std::exception& error) {
        std::wstring message=utf8_to_wide(error.what());
        MessageBoxW(owner,message.c_str(),L"无法生成",MB_OK|MB_ICONERROR);
    }
}

void draw_centered_text(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& bounds,
    float size,
    bool bold=false,
    Gdiplus::Color color=Gdiplus::Color(255,245,248,255)
) {
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::Font font(
        &family,size,bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel
    );
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(text.c_str(),-1,&font,bounds,&format,&brush);
}

bool progression_is_complete() {
    for (const auto& chord:displayed_progression.chords) {
        if (chord.notes.empty()) {
            return false;
        }
    }
    return true;
}

unsigned int chord_pitch_mask(const std::string& notes) {
    unsigned int mask=0;
    for (int pitch_class:ncnl::parse_chord(notes)) {
        mask|=1u<<pitch_class;
    }
    return mask;
}

void recalculate_progression_quality() {
    if (!progression_is_complete()) {
        displayed_progression.quality_score=0.0;
        return;
    }
    double total=0.0;
    int repeated=0;
    for (int position=0;position<4;++position) {
        int next_position=(position+1)%4;
        const std::string& current=displayed_progression.chords[position].notes;
        const std::string& next=
            displayed_progression.chords[next_position].notes;
        total+=ncnl::chord_progression_score(current_mode,current,next);
        repeated+=chord_pitch_mask(current)==chord_pitch_mask(next);
    }
    displayed_progression.quality_score=total/4.0-14.0*repeated;
}

std::string pitch_classes_to_text(const ncnl::Chord& chord) {
    static const std::array<const char*,12> names={{
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    }};
    std::ostringstream text;
    for (std::size_t index=0;index<chord.size();++index) {
        if (index>0) {
            text<<' ';
        }
        text<<names[static_cast<std::size_t>(chord[index])];
    }
    return text.str();
}

void hide_chord_editor();

void refresh_progression_display() {
    recalculate_progression_quality();
    main_background_dirty=true;
    InvalidateRect(main_window,nullptr,FALSE);
}

void clear_chord_at_client_point(HWND window,POINT point) {
    int position=piano_position_at_client_point(window,point,false);
    if (position<0 || displayed_progression.chords[position].notes.empty()) {
        return;
    }
    stop_midi_playback();
    hide_chord_editor();
    displayed_progression.chords[position]={"",0.0};
    refresh_progression_display();
}

bool begin_midi_note_drag(HWND window,POINT point) {
    int position=-1;
    int pitch=-1;
    if (!piano_note_at_client_point(window,point,position,pitch) ||
        displayed_progression.chords[position].notes.empty()) {
        return false;
    }
    ncnl::Chord chord=ncnl::parse_chord(
        displayed_progression.chords[position].notes
    );
    if (std::find(chord.begin(),chord.end(),pitch)==chord.end()) {
        return false;
    }
    dragged_midi_position=position;
    dragged_midi_pitch=pitch;
    stop_midi_playback();
    SetCapture(window);
    SetCursor(LoadCursorW(nullptr,IDC_SIZENS));
    return true;
}

void update_midi_note_drag(HWND window,POINT point) {
    if (dragged_midi_position<0 || dragged_midi_pitch<0) {
        return;
    }
    int hovered_position=-1;
    int target_pitch=-1;
    if (!piano_note_at_client_point(
            window,point,hovered_position,target_pitch
        ) || target_pitch==dragged_midi_pitch) {
        return;
    }

    auto& displayed=displayed_progression.chords[dragged_midi_position];
    ncnl::Chord chord=ncnl::parse_chord(displayed.notes);
    if (std::find(chord.begin(),chord.end(),target_pitch)!=chord.end()) {
        return;
    }
    auto source=std::find(chord.begin(),chord.end(),dragged_midi_pitch);
    if (source==chord.end()) {
        return;
    }
    *source=target_pitch;
    std::string notes=pitch_classes_to_text(chord);
    double emotion=ncnl::chord_emotion_score(current_mode,notes);
    displayed={notes,emotion};
    dragged_midi_pitch=target_pitch;
    refresh_progression_display();
    SetCursor(LoadCursorW(nullptr,IDC_SIZENS));
}

void end_midi_note_drag(HWND window,POINT point) {
    if (dragged_midi_position<0) {
        return;
    }
    update_midi_note_drag(window,point);
    dragged_midi_position=-1;
    dragged_midi_pitch=-1;
    if (GetCapture()==window) {
        ReleaseCapture();
    }
}

void hide_chord_editor() {
    editing_chord=-1;
    if (chord_editor) {
        ShowWindow(chord_editor,SW_HIDE);
    }
}

bool commit_chord_editor(bool show_error) {
    if (editing_chord<0 || editing_chord>=4) {
        return true;
    }
    int position=editing_chord;
    std::string notes=wide_to_utf8(get_window_text(chord_editor));
    std::size_t first=notes.find_first_not_of(" \t\r\n");
    std::size_t last=notes.find_last_not_of(" \t\r\n");
    if (first==std::string::npos) {
        notes.clear();
    }
    else {
        notes=notes.substr(first,last-first+1);
    }

    try {
        if (notes.empty()) {
            stop_midi_playback();
            displayed_progression.chords[position]={"",0.0};
        }
        else {
            ncnl::parse_chord(notes);
            double score=ncnl::chord_emotion_score(current_mode,notes);
            stop_midi_playback();
            displayed_progression.chords[position]={notes,score};
        }
        recalculate_progression_quality();
        hide_chord_editor();
        main_background_dirty=true;
        RedrawWindow(
            main_window,nullptr,nullptr,
            RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN
        );
        return true;
    }
    catch (const std::exception& error) {
        if (!show_error) {
            hide_chord_editor();
            return false;
        }
        std::wstring message=utf8_to_wide(error.what());
        MessageBoxW(
            main_window,message.c_str(),L"和弦内音格式错误",MB_OK|MB_ICONERROR
        );
        SetFocus(chord_editor);
        SendMessageW(chord_editor,EM_SETSEL,0,-1);
        return false;
    }
}

void begin_chord_edit(HWND window,int position) {
    if (position<0 || position>=4) {
        return;
    }
    if (editing_chord>=0) {
        commit_chord_editor(false);
    }

    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    int offset_x=static_cast<int>(std::lround((width-DESIGN_WIDTH*scale)/2.0));
    int offset_y=static_cast<int>(std::lround((height-DESIGN_HEIGHT*scale)/2.0));
    double section_width=(900.0-82.0)/4.0;
    int x=offset_x+static_cast<int>(std::lround(
        (50.0+82.0+position*section_width+5.0)*scale
    ));
    int y=offset_y+static_cast<int>(std::lround(300.0*scale));
    int editor_width=static_cast<int>(std::lround((section_width-10.0)*scale));
    int editor_height=static_cast<int>(std::lround(42.0*scale));

    editing_chord=position;
    SetWindowTextW(
        chord_editor,
        utf8_to_wide(displayed_progression.chords[position].notes).c_str()
    );
    MoveWindow(chord_editor,x,y,editor_width,editor_height,TRUE);
    int radius=std::max(6,static_cast<int>(std::lround(12*scale)));
    SetWindowRgn(
        chord_editor,
        CreateRoundRectRgn(0,0,editor_width+1,editor_height+1,radius,radius),
        TRUE
    );
    SendMessageW(
        chord_editor,WM_SETFONT,reinterpret_cast<WPARAM>(card_font),TRUE
    );
    ShowWindow(chord_editor,SW_SHOW);
    SetFocus(chord_editor);
    SendMessageW(chord_editor,EM_SETSEL,0,-1);
}

LRESULT CALLBACK chord_editor_procedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    if (message==WM_KEYDOWN && w_param==VK_RETURN) {
        commit_chord_editor(true);
        return 0;
    }
    if (message==WM_KEYDOWN && w_param==VK_ESCAPE) {
        hide_chord_editor();
        SetFocus(main_window);
        return 0;
    }
    return CallWindowProcW(
        chord_editor_procedure_original,window,message,w_param,l_param
    );
}

void draw_piano_roll(
    Gdiplus::Graphics& graphics,
    float offset_x,
    float offset_y,
    float scale
) {
    static const std::array<const wchar_t*,12> pitch_names={{
        L"C",L"C#",L"D",L"D#",L"E",L"F",
        L"F#",L"G",L"G#",L"A",L"A#",L"B"
    }};
    const float left=offset_x+50.0f*scale;
    const float top=offset_y+295.0f*scale;
    const float roll_width=900.0f*scale;
    const float roll_height=350.0f*scale;
    const float keyboard_width=82.0f*scale;
    const float header_height=52.0f*scale;
    const float grid_left=left+keyboard_width;
    const float grid_top=top+header_height;
    const float grid_width=roll_width-keyboard_width;
    const float grid_height=roll_height-header_height;
    const float section_width=grid_width/4.0f;
    const float row_height=grid_height/12.0f;
    const float radius=18.0f*scale;

    Gdiplus::RectF outer(left,top,roll_width,roll_height);
    Gdiplus::GraphicsPath rounded;
    add_rounded_rectangle(rounded,outer,radius);
    Gdiplus::GraphicsState state=graphics.Save();
    graphics.SetClip(&rounded);

    Gdiplus::SolidBrush frame(Gdiplus::Color(238,22,27,34));
    Gdiplus::SolidBrush header(Gdiplus::Color(245,35,42,53));
    graphics.FillRectangle(&frame,outer);
    graphics.FillRectangle(&header,left,top,roll_width,header_height);

    for (int lane=0;lane<12;++lane) {
        int pitch_class=11-lane;
        bool black_key=pitch_class==1 || pitch_class==3 || pitch_class==6 ||
                       pitch_class==8 || pitch_class==10;
        float y=grid_top+lane*row_height;
        Gdiplus::SolidBrush lane_brush(
            black_key ? Gdiplus::Color(240,25,29,36)
                      : Gdiplus::Color(232,43,48,58)
        );
        graphics.FillRectangle(&lane_brush,grid_left,y,grid_width,row_height);

        Gdiplus::SolidBrush key_background(Gdiplus::Color(255,226,230,236));
        graphics.FillRectangle(&key_background,left,y,keyboard_width,row_height);
        if (black_key) {
            Gdiplus::SolidBrush black_key_brush(Gdiplus::Color(255,36,40,47));
            graphics.FillRectangle(
                &black_key_brush,left,y,53.0f*scale,row_height
            );
        }

        std::wostringstream key_name;
        key_name<<pitch_names[static_cast<std::size_t>(pitch_class)]<<L"4";
        draw_centered_text(
            graphics,key_name.str(),
            Gdiplus::RectF(
                left+52.0f*scale,y,30.0f*scale,row_height
            ),
            11.0f*scale,true,Gdiplus::Color(255,35,42,52)
        );
    }

    Gdiplus::Pen row_line(Gdiplus::Color(130,104,115,132),1.0f);
    for (int lane=0;lane<=12;++lane) {
        float y=grid_top+lane*row_height;
        graphics.DrawLine(&row_line,left,y,left+roll_width,y);
    }
    Gdiplus::Pen keyboard_edge(Gdiplus::Color(230,150,166,190),2.0f*scale);
    graphics.DrawLine(
        &keyboard_edge,grid_left,top,grid_left,top+roll_height
    );

    Gdiplus::SolidBrush note_fill(Gdiplus::Color(255,55,206,235));
    Gdiplus::Pen note_edge(Gdiplus::Color(255,166,244,255),1.5f*scale);
    for (int position=0;position<4;++position) {
        const auto& chord=displayed_progression.chords[position];
        std::wstring caption=chord.notes.empty()
            ? L"双击输入和弦内音"
            : utf8_to_wide(chord.notes);
        draw_centered_text(
            graphics,caption,
            Gdiplus::RectF(
                grid_left+position*section_width,top,
                section_width,header_height
            ),
            chord.notes.empty() ? 12.0f*scale : 16.0f*scale,
            !chord.notes.empty(),
            chord.notes.empty() ? Gdiplus::Color(180,205,214,230)
                                : Gdiplus::Color(255,245,248,255)
        );

        if (chord.notes.empty()) {
            continue;
        }
        ncnl::Chord notes=ncnl::parse_chord(chord.notes);
        for (int pitch_class:notes) {
            int lane=11-pitch_class;
            float note_x=grid_left+position*section_width+10.0f*scale;
            float note_y=grid_top+lane*row_height+2.0f*scale;
            float note_width=section_width-20.0f*scale;
            float note_height=std::max(3.0f,row_height-4.0f*scale);
            Gdiplus::RectF note_rect(
                note_x,note_y,note_width,note_height
            );
            Gdiplus::GraphicsPath note_path;
            add_rounded_rectangle(note_path,note_rect,5.0f*scale);
            graphics.FillPath(&note_fill,&note_path);
            graphics.DrawPath(&note_edge,&note_path);
            draw_centered_text(
                graphics,pitch_names[static_cast<std::size_t>(pitch_class)],
                note_rect,12.0f*scale,true,Gdiplus::Color(255,12,52,66)
            );
        }
    }

    graphics.Restore(state);
    Gdiplus::Pen outline(Gdiplus::Color(255,165,180,205),2.0f*scale);
    graphics.DrawPath(&outline,&rounded);

    for (int transition=0;transition<3;++transition) {
        // 箭头对准钢琴卷帘中相邻两个和弦区域的真实分界线。
        float boundary=132.0f+(transition+1)*(818.0f/4.0f);
        float x=offset_x+(boundary-25.0f)*scale;
        float y=offset_y+675.0f*scale;
        float size=50.0f*scale;
        if (arrow_image) {
            graphics.DrawImage(
                arrow_image.get(),Gdiplus::RectF(x,y,size,size),
                0.0f,0.0f,
                static_cast<float>(arrow_image->GetWidth()),
                static_cast<float>(arrow_image->GetHeight()),
                Gdiplus::UnitPixel
            );
        }
        else {
            draw_centered_text(
                graphics,L"↑",Gdiplus::RectF(x,y,size,size),32.0f*scale,true
            );
        }
    }

    if (progression_is_complete()) {
        std::wostringstream quality;
        quality<<L"进行质量 "<<std::fixed<<std::setprecision(1)
               <<displayed_progression.quality_score;
        draw_centered_text(
            graphics,quality.str(),
            Gdiplus::RectF(
                offset_x+330.0f*scale,offset_y+945.0f*scale,
                340.0f*scale,35.0f*scale
            ),
            16.0f*scale,true
        );
    }
}

void draw_background(HWND window,HDC dc,bool draw_details) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;

    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    Gdiplus::LinearGradientBrush base(
        Gdiplus::Point(0,0),
        Gdiplus::Point(width,height),
        Gdiplus::Color(255,24,29,43),
        Gdiplus::Color(255,63,45,83)
    );
    graphics.FillRectangle(&base,0,0,width,height);

    // 拖拽期间只绘制轻量底色；背景缩放和卡片布局在鼠标松开后统一重绘。
    if (!draw_details) {
        return;
    }

    if (background_image) {
        UINT image_width=background_image->GetWidth();
        UINT image_height=background_image->GetHeight();
        UINT square=std::min(image_width,image_height);
        INT source_x=static_cast<INT>((image_width-square)/2);
        INT source_y=static_cast<INT>((image_height-square)/2);

        Gdiplus::ColorMatrix opacity_matrix={
            1.0f,0.0f,0.0f,0.0f,0.0f,
            0.0f,1.0f,0.0f,0.0f,0.0f,
            0.0f,0.0f,1.0f,0.0f,0.0f,
            0.0f,0.0f,0.0f,0.40f,0.0f,
            0.0f,0.0f,0.0f,0.0f,1.0f,
        };
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(
            &opacity_matrix,
            Gdiplus::ColorMatrixFlagsDefault,
            Gdiplus::ColorAdjustTypeBitmap
        );
        graphics.DrawImage(
            background_image.get(),
            Gdiplus::Rect(0,0,width,height),
            source_x,source_y,static_cast<INT>(square),static_cast<INT>(square),
            Gdiplus::UnitPixel,&attributes
        );
    }

    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    float offset_x=static_cast<float>((width-DESIGN_WIDTH*scale)/2.0);
    float offset_y=static_cast<float>((height-DESIGN_HEIGHT*scale)/2.0);
    draw_piano_roll(
        graphics,offset_x,offset_y,static_cast<float>(scale)
    );
}

void destroy_main_background_buffer() {
    if (main_background_dc && main_background_old_bitmap) {
        SelectObject(main_background_dc,main_background_old_bitmap);
    }
    if (main_background_bitmap) {
        DeleteObject(main_background_bitmap);
    }
    if (main_background_dc) {
        DeleteDC(main_background_dc);
    }
    main_background_dc=nullptr;
    main_background_bitmap=nullptr;
    main_background_old_bitmap=nullptr;
    main_background_width=0;
    main_background_height=0;
}

bool ensure_main_background_buffer(HWND window,HDC reference) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    if (width<=0 || height<=0) {
        return false;
    }
    if (!main_background_dc || width!=main_background_width ||
        height!=main_background_height) {
        destroy_main_background_buffer();
        main_background_dc=CreateCompatibleDC(reference);
        main_background_bitmap=CreateCompatibleBitmap(reference,width,height);
        if (!main_background_dc || !main_background_bitmap) {
            destroy_main_background_buffer();
            return false;
        }
        main_background_old_bitmap=SelectObject(
            main_background_dc,main_background_bitmap
        );
        main_background_width=width;
        main_background_height=height;
        main_background_dirty=true;
    }
    if (main_background_dirty) {
        draw_background(window,main_background_dc,!interactive_resize);
        main_background_dirty=false;
    }
    return true;
}

void paint_window(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc=BeginPaint(window,&paint);
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    if (width>0 && height>0 && ensure_main_background_buffer(window,dc)) {
        BitBlt(dc,0,0,width,height,main_background_dc,0,0,SRCCOPY);
    }
    EndPaint(window,&paint);
}

COLORREF preset_color(int preset,bool pressed) {
    static const std::array<COLORREF,5> colors={{
        RGB(48,52,70),
        RGB(78,72,103),
        RGB(123,108,132),
        RGB(185,147,103),
        RGB(231,190,82),
    }};
    COLORREF color=colors[static_cast<std::size_t>(preset)];
    if (!pressed) {
        return color;
    }
    return RGB(
        GetRValue(color)*4/5,
        GetGValue(color)*4/5,
        GetBValue(color)*4/5
    );
}

void draw_square_button(const DRAWITEMSTRUCT* item) {
    RECT bounds=item->rcItem;
    bool pressed=(item->itemState&ODS_SELECTED)!=0;
    int id=static_cast<int>(item->CtlID);

    // Owner-drawn BUTTON 的未绘制区域会保留系统按钮底色。先把父窗口在
    // 对应位置的背景绘入控件 DC，圆角之外才能真正透出背景图。
    POINT control_origin={0,0};
    ClientToScreen(item->hwndItem,&control_origin);
    ScreenToClient(main_window,&control_origin);
    if (ensure_main_background_buffer(main_window,item->hDC)) {
        BitBlt(
            item->hDC,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
            main_background_dc,control_origin.x,control_origin.y,SRCCOPY
        );
    }

    COLORREF fill=pressed ? RGB(55,91,128) : RGB(67,112,158);
    Gdiplus::Image* button_image=nullptr;

    if (id>=ID_PRESET_BASE && id<ID_PRESET_BASE+5) {
        int preset=id-ID_PRESET_BASE;
        fill=preset_color(preset,pressed);
        button_image=emotion_images[static_cast<std::size_t>(preset)].get();
    }
    else if (id>=ID_SLOT_BASE && id<ID_SLOT_BASE+4) {
        int position=id-ID_SLOT_BASE;
        fill=position==active_slot ? RGB(54,112,171) : RGB(50,58,77);
        int preset=slot_presets[static_cast<std::size_t>(position)];
        if (preset>=0 && preset<5) {
            button_image=emotion_images[static_cast<std::size_t>(preset)].get();
            fill=preset_color(preset,pressed);
        }
    }
    else if (id==ID_SETTINGS) {
        fill=pressed ? RGB(80,69,112) : RGB(105,88,145);
    }
    else if (id==ID_GENERATE) {
        fill=pressed ? RGB(44,116,87) : RGB(55,151,111);
    }

    if (button_image) {
        Gdiplus::Graphics graphics(item->hDC);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        int image_width=std::max(1,static_cast<int>(bounds.right-bounds.left));
        int image_height=std::max(1,static_cast<int>(bounds.bottom-bounds.top));
        Gdiplus::GraphicsPath clip_path;
        add_rounded_rectangle(
            clip_path,
            Gdiplus::RectF(
                static_cast<float>(bounds.left),
                static_cast<float>(bounds.top),
                static_cast<float>(image_width),static_cast<float>(image_height)
            ),
            std::max(7.0f,16.0f*static_cast<float>(current_scale))
        );
        graphics.SetClip(&clip_path);
        Gdiplus::SolidBrush image_background(
            Gdiplus::Color(
                255,GetRValue(fill),GetGValue(fill),GetBValue(fill)
            )
        );
        graphics.FillPath(&image_background,&clip_path);
        graphics.DrawImage(
            button_image,
            Gdiplus::Rect(
                bounds.left,bounds.top,image_width,image_height
            ),
            0,0,static_cast<INT>(button_image->GetWidth()),
            static_cast<INT>(button_image->GetHeight()),Gdiplus::UnitPixel
        );
        if (pressed) {
            Gdiplus::SolidBrush shade(Gdiplus::Color(75,0,0,0));
            graphics.FillRectangle(
                &shade,static_cast<INT>(bounds.left),static_cast<INT>(bounds.top),
                static_cast<INT>(bounds.right-bounds.left),
                static_cast<INT>(bounds.bottom-bounds.top)
            );
        }
        graphics.ResetClip();
        return;
    }

    HBRUSH fill_brush=CreateSolidBrush(fill);
    HGDIOBJ old_brush=SelectObject(item->hDC,fill_brush);
    HGDIOBJ old_pen=SelectObject(item->hDC,GetStockObject(NULL_PEN));
    int corner=std::max(14,static_cast<int>(std::lround(32*current_scale)));
    RoundRect(
        item->hDC,bounds.left,bounds.top,bounds.right,bounds.bottom,corner,corner
    );
    SelectObject(item->hDC,old_pen);
    SelectObject(item->hDC,old_brush);
    DeleteObject(fill_brush);

    wchar_t text[128]={};
    GetWindowTextW(item->hwndItem,text,128);
    SetBkMode(item->hDC,TRANSPARENT);
    SetTextColor(item->hDC,RGB(255,255,255));
    HFONT font=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));
    HGDIOBJ old_font=SelectObject(item->hDC,font);
    DrawTextW(item->hDC,text,-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(item->hDC,old_font);
}

void enforce_square_resize(HWND window,WPARAM edge,RECT* proposed) {
    RECT current_window{};
    RECT current_client{};
    GetWindowRect(window,&current_window);
    GetClientRect(window,&current_client);
    int frame_width=(current_window.right-current_window.left)-
        (current_client.right-current_client.left);
    int frame_height=(current_window.bottom-current_window.top)-
        (current_client.bottom-current_client.top);

    int proposed_client_width=std::max(
        MIN_CLIENT_SIZE,
        static_cast<int>(proposed->right-proposed->left)-frame_width
    );
    int proposed_client_height=std::max(
        MIN_CLIENT_SIZE,
        static_cast<int>(proposed->bottom-proposed->top)-frame_height
    );
    int side;
    if (edge==WMSZ_LEFT || edge==WMSZ_RIGHT) {
        side=proposed_client_width;
    }
    else if (edge==WMSZ_TOP || edge==WMSZ_BOTTOM) {
        side=proposed_client_height;
    }
    else {
        side=(proposed_client_width+proposed_client_height)/2;
    }

    int outer_width=side+frame_width;
    int outer_height=side+frame_height;
    switch (edge) {
    case WMSZ_LEFT:
        proposed->left=proposed->right-outer_width;
        proposed->bottom=proposed->top+outer_height;
        break;
    case WMSZ_RIGHT:
        proposed->right=proposed->left+outer_width;
        proposed->bottom=proposed->top+outer_height;
        break;
    case WMSZ_TOP:
        proposed->top=proposed->bottom-outer_height;
        proposed->right=proposed->left+outer_width;
        break;
    case WMSZ_BOTTOM:
        proposed->bottom=proposed->top+outer_height;
        proposed->right=proposed->left+outer_width;
        break;
    case WMSZ_TOPLEFT:
        proposed->left=proposed->right-outer_width;
        proposed->top=proposed->bottom-outer_height;
        break;
    case WMSZ_TOPRIGHT:
        proposed->right=proposed->left+outer_width;
        proposed->top=proposed->bottom-outer_height;
        break;
    case WMSZ_BOTTOMLEFT:
        proposed->left=proposed->right-outer_width;
        proposed->bottom=proposed->top+outer_height;
        break;
    case WMSZ_BOTTOMRIGHT:
    default:
        proposed->right=proposed->left+outer_width;
        proposed->bottom=proposed->top+outer_height;
        break;
    }
}

const std::array<std::size_t,5> RADAR_TO_WEIGHT={{
    static_cast<std::size_t>(ncnl::ProgressionWeight::common_tone),
    static_cast<std::size_t>(ncnl::ProgressionWeight::root_motion),
    static_cast<std::size_t>(ncnl::ProgressionWeight::tonal_attraction),
    static_cast<std::size_t>(ncnl::ProgressionWeight::modal_consistency),
    static_cast<std::size_t>(ncnl::ProgressionWeight::voice_leading),
}};

const std::array<Gdiplus::PointF,5> RADAR_VERTICES={{
    Gdiplus::PointF(395.0f,86.0f),
    Gdiplus::PointF(675.0f,271.0f),
    Gdiplus::PointF(587.0f,639.0f),
    // 原图“调式一致性”的轴端点更靠左下，位于外层粗线的实际顶角。
    Gdiplus::PointF(290.0f,654.0f),
    Gdiplus::PointF(118.0f,271.0f),
}};
const Gdiplus::PointF RADAR_CENTER(400.0f,375.0f);

struct ConfigurationTransform {
    float scale;
    float offset_x;
    float offset_y;
};

ConfigurationTransform configuration_transform(HWND window) {
    RECT client{};
    GetClientRect(window,&client);
    float width=static_cast<float>(client.right-client.left);
    float height=static_cast<float>(client.bottom-client.top);
    float scale=std::max(0.25f,std::min(
        width/SETTINGS_DESIGN_SIZE,height/SETTINGS_DESIGN_SIZE
    ));
    return {
        scale,
        (width-SETTINGS_DESIGN_SIZE*scale)/2.0f,
        (height-SETTINGS_DESIGN_SIZE*scale)/2.0f,
    };
}

Gdiplus::PointF configuration_logical_point(HWND window,LPARAM l_param) {
    ConfigurationTransform transform=configuration_transform(window);
    return Gdiplus::PointF(
        (GET_X_LPARAM(l_param)-transform.offset_x)/transform.scale,
        (GET_Y_LPARAM(l_param)-transform.offset_y)/transform.scale
    );
}

Gdiplus::PointF radar_point(int axis) {
    const auto& weights=ncnl::progression_weights().values;
    double value=weights[RADAR_TO_WEIGHT[static_cast<std::size_t>(axis)]];
    const Gdiplus::PointF& vertex=RADAR_VERTICES[static_cast<std::size_t>(axis)];
    return Gdiplus::PointF(
        RADAR_CENTER.X+static_cast<float>((vertex.X-RADAR_CENTER.X)*value),
        RADAR_CENTER.Y+static_cast<float>((vertex.Y-RADAR_CENTER.Y)*value)
    );
}

bool point_in_logical_rect(
    const Gdiplus::PointF& point,float x,float y,float width,float height
) {
    return point.X>=x && point.X<=x+width && point.Y>=y && point.Y<=y+height;
}

void draw_configuration_contents(HWND window,HDC dc) {
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    Gdiplus::LinearGradientBrush background(
        Gdiplus::Point(0,0),Gdiplus::Point(width,height),
        Gdiplus::Color(255,23,29,43),Gdiplus::Color(255,56,44,76)
    );
    graphics.FillRectangle(&background,0,0,width,height);

    ConfigurationTransform transform=configuration_transform(window);
    Gdiplus::GraphicsState state=graphics.Save();
    graphics.TranslateTransform(transform.offset_x,transform.offset_y);
    graphics.ScaleTransform(transform.scale,transform.scale);

    if (configuration_tab==0) {
        if (pentagon_image) {
            graphics.DrawImage(
                pentagon_image.get(),Gdiplus::RectF(70.0f,20.0f,660.0f,660.0f),
                0.0f,0.0f,static_cast<float>(pentagon_image->GetWidth()),
                static_cast<float>(pentagon_image->GetHeight()),Gdiplus::UnitPixel
            );
        }
        Gdiplus::Pen axis_pen(Gdiplus::Color(165,91,111,132),1.5f);
        for (const auto& vertex:RADAR_VERTICES) {
            graphics.DrawLine(&axis_pen,RADAR_CENTER,vertex);
        }

        std::array<Gdiplus::PointF,5> points;
        for (int axis=0;axis<5;++axis) {
            points[static_cast<std::size_t>(axis)]=radar_point(axis);
        }
        Gdiplus::GraphicsPath polygon;
        polygon.AddPolygon(points.data(),static_cast<INT>(points.size()));
        Gdiplus::SolidBrush polygon_fill(Gdiplus::Color(51,92,205,238));
        Gdiplus::Pen polygon_edge(Gdiplus::Color(230,126,226,250),2.5f);
        graphics.FillPath(&polygon_fill,&polygon);
        graphics.DrawPath(&polygon_edge,&polygon);

        Gdiplus::SolidBrush point_fill(Gdiplus::Color(255,224,249,255));
        Gdiplus::Pen point_edge(Gdiplus::Color(255,36,127,166),2.0f);
        for (int axis=0;axis<5;++axis) {
            const auto& point=points[static_cast<std::size_t>(axis)];
            graphics.FillEllipse(&point_fill,point.X-8.0f,point.Y-8.0f,16.0f,16.0f);
            graphics.DrawEllipse(&point_edge,point.X-8.0f,point.Y-8.0f,16.0f,16.0f);
        }
    }
    else {
        draw_centered_text(
            graphics,L"外观与背景",Gdiplus::RectF(120.0f,110.0f,560.0f,70.0f),
            34.0f,true
        );
        draw_centered_text(
            graphics,L"背景图片将居中裁切为 1:1，并缓存至 assets/bg.png",
            Gdiplus::RectF(90.0f,205.0f,620.0f,60.0f),22.0f,false
        );
        Gdiplus::RectF button_bounds(230.0f,310.0f,340.0f,78.0f);
        Gdiplus::GraphicsPath button_path;
        add_rounded_rectangle(button_path,button_bounds,16.0f);
        Gdiplus::SolidBrush button_fill(Gdiplus::Color(235,80,115,160));
        Gdiplus::Pen button_edge(Gdiplus::Color(255,178,218,242),2.0f);
        graphics.FillPath(&button_fill,&button_path);
        graphics.DrawPath(&button_edge,&button_path);
        draw_centered_text(graphics,L"选择 PNG 背景图",button_bounds,23.0f,true);
    }

    const std::array<std::wstring,2> tab_titles={{
        L"和弦行进逻辑参数调整",L"外观设置"
    }};
    for (int tab=0;tab<2;++tab) {
        Gdiplus::RectF bounds(35.0f+370.0f*tab,724.0f,360.0f,55.0f);
        Gdiplus::GraphicsPath path;
        add_rounded_rectangle(path,bounds,13.0f);
        Gdiplus::SolidBrush fill(
            tab==configuration_tab
                ? Gdiplus::Color(245,57,142,181)
                : Gdiplus::Color(220,49,56,76)
        );
        Gdiplus::Pen edge(Gdiplus::Color(230,170,207,230),1.5f);
        graphics.FillPath(&fill,&path);
        graphics.DrawPath(&edge,&path);
        draw_centered_text(graphics,tab_titles[tab],bounds,18.0f,tab==configuration_tab);
    }
    graphics.Restore(state);
}

void paint_configuration_window(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc=BeginPaint(window,&paint);
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    if (width>0 && height>0) {
        HDC buffer=CreateCompatibleDC(dc);
        HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);
        HGDIOBJ old_bitmap=SelectObject(buffer,bitmap);
        draw_configuration_contents(window,buffer);
        BitBlt(dc,0,0,width,height,buffer,0,0,SRCCOPY);
        SelectObject(buffer,old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
    }
    EndPaint(window,&paint);
}

int nearest_radar_axis(const Gdiplus::PointF& point) {
    int nearest=-1;
    float best_distance=22.0f*22.0f;
    for (int axis=0;axis<5;++axis) {
        Gdiplus::PointF handle=radar_point(axis);
        float dx=point.X-handle.X;
        float dy=point.Y-handle.Y;
        float distance=dx*dx+dy*dy;
        if (distance<=best_distance) {
            best_distance=distance;
            nearest=axis;
        }
    }
    if (nearest>=0) {
        return nearest;
    }

    // 当若干权重为 0 时，它们的点会重叠在中心；此时也允许直接点击
    // 对应轴线来选中该维度，避免零权重点无法再次被单独拉出。
    best_distance=18.0f*18.0f;
    for (int axis=0;axis<5;++axis) {
        const auto& vertex=RADAR_VERTICES[static_cast<std::size_t>(axis)];
        float axis_x=vertex.X-RADAR_CENTER.X;
        float axis_y=vertex.Y-RADAR_CENTER.Y;
        float length_squared=axis_x*axis_x+axis_y*axis_y;
        float projection=((point.X-RADAR_CENTER.X)*axis_x+
                          (point.Y-RADAR_CENTER.Y)*axis_y)/length_squared;
        if (projection<0.08f || projection>1.05f) {
            continue;
        }
        float projected_x=RADAR_CENTER.X+projection*axis_x;
        float projected_y=RADAR_CENTER.Y+projection*axis_y;
        float dx=point.X-projected_x;
        float dy=point.Y-projected_y;
        float distance=dx*dx+dy*dy;
        if (distance<best_distance) {
            best_distance=distance;
            nearest=axis;
        }
    }
    return nearest;
}

void update_dragged_weight(HWND window,const Gdiplus::PointF& point) {
    if (dragged_weight_axis<0 || dragged_weight_axis>=5) {
        return;
    }
    const Gdiplus::PointF& vertex=
        RADAR_VERTICES[static_cast<std::size_t>(dragged_weight_axis)];
    float axis_x=vertex.X-RADAR_CENTER.X;
    float axis_y=vertex.Y-RADAR_CENTER.Y;
    double value=((point.X-RADAR_CENTER.X)*axis_x+
                  (point.Y-RADAR_CENTER.Y)*axis_y)/
                 (axis_x*axis_x+axis_y*axis_y);
    ncnl::adjust_progression_weight(
        RADAR_TO_WEIGHT[static_cast<std::size_t>(dragged_weight_axis)],value
    );
    recalculate_progression_quality();
    main_background_dirty=true;
    InvalidateRect(main_window,nullptr,FALSE);
    InvalidateRect(window,nullptr,FALSE);
}

LRESULT CALLBACK configuration_window_procedure(
    HWND window,UINT message,WPARAM w_param,LPARAM l_param
) {
    switch (message) {
    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;
    case WM_ENTERSIZEMOVE:
        configuration_interactive_resize=true;
        return 0;
    case WM_EXITSIZEMOVE:
        configuration_interactive_resize=false;
        InvalidateRect(window,nullptr,FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(l_param);
        RECT desired={0,0,MIN_CLIENT_SIZE,MIN_CLIENT_SIZE};
        DWORD style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
        DWORD ex_style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
        AdjustWindowRectEx(&desired,style,FALSE,ex_style);
        info->ptMinTrackSize.x=desired.right-desired.left;
        info->ptMinTrackSize.y=desired.bottom-desired.top;
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_configuration_window(window);
        return 0;
    case WM_LBUTTONDOWN: {
        Gdiplus::PointF point=configuration_logical_point(window,l_param);
        if (point_in_logical_rect(point,35.0f,724.0f,360.0f,55.0f)) {
            configuration_tab=0;
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if (point_in_logical_rect(point,405.0f,724.0f,360.0f,55.0f)) {
            configuration_tab=1;
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if (configuration_tab==1 &&
            point_in_logical_rect(point,230.0f,310.0f,340.0f,78.0f)) {
            choose_background(window);
            return 0;
        }
        if (configuration_tab==0) {
            dragged_weight_axis=nearest_radar_axis(point);
            if (dragged_weight_axis>=0) {
                SetCapture(window);
                update_dragged_weight(window,point);
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (dragged_weight_axis>=0 && GetCapture()==window) {
            update_dragged_weight(window,configuration_logical_point(window,l_param));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (dragged_weight_axis>=0) {
            update_dragged_weight(window,configuration_logical_point(window,l_param));
            dragged_weight_axis=-1;
            if (GetCapture()==window) {
                ReleaseCapture();
            }
            ncnl::save_progression_config(progression_config_path);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (dragged_weight_axis>=0) {
            dragged_weight_axis=-1;
            ncnl::save_progression_config(progression_config_path);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ncnl::save_progression_config(progression_config_path);
        configuration_window=nullptr;
        return 0;
    }
    return DefWindowProcW(window,message,w_param,l_param);
}

void open_configuration_window(HWND owner) {
    if (configuration_window) {
        ShowWindow(configuration_window,SW_RESTORE);
        SetForegroundWindow(configuration_window);
        return;
    }
    DWORD style=WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX;
    RECT size={0,0,SETTINGS_DESIGN_SIZE,SETTINGS_DESIGN_SIZE};
    AdjustWindowRectEx(&size,style,FALSE,0);
    configuration_window=CreateWindowExW(
        0,L"NoChordNoLifeConfigurationWindow",L"NoChordNoLife 配置",
        style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,
        owner,nullptr,GetModuleHandleW(nullptr),nullptr
    );
    if (!configuration_window) {
        MessageBoxW(owner,L"无法创建配置窗口。",L"配置",MB_OK|MB_ICONERROR);
        return;
    }
    ShowWindow(configuration_window,SW_SHOW);
    UpdateWindow(configuration_window);
}

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    switch (message) {
    case WM_CREATE: {
        main_window=window;
        chord_editor=CreateWindowExW(
            0,L"EDIT",L"",
            WS_CHILD|WS_BORDER|ES_CENTER|ES_AUTOHSCROLL,
            0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr
        );
        chord_editor_procedure_original=reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(
                chord_editor,GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(chord_editor_procedure)
            )
        );
        create_control(
            L"STATIC",L"当前调式：C Ionian",
            WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,
            45,30,420,55,window,0,FontKind::title
        );
        settings_button=create_control(
            L"BUTTON",L"配置",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            835,32,120,52,window,ID_SETTINGS
        );

        create_control(
            L"BUTTON",L"预设",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            55,125,120,115,window,ID_PRESET_ACTION,FontKind::card
        );

        for (int preset=0;preset<5;++preset) {
            preset_buttons[preset]=create_control(
                L"BUTTON",L"",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                210+145*preset,125,115,115,window,
                ID_PRESET_BASE+preset,FontKind::card
            );
            preset_button_procedures[preset]=reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(
                    preset_buttons[preset],GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(preset_button_procedure)
                )
            );
        }

        for (int position=0;position<4;++position) {
            chord_header_targets[position]=create_control(
                L"STATIC",L"",
                WS_CHILD|WS_VISIBLE|SS_NOTIFY,
                132+static_cast<int>(std::lround(204.5*position)),
                295,205,52,window,
                ID_CHORD_HEADER_BASE+position,FontKind::card
            );
        }

        for (int position=0;position<4;++position) {
            int x=SLOT_X[static_cast<std::size_t>(position)];
            slot_buttons[position]=create_control(
                L"BUTTON",L"",
                WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                x,SLOT_Y,SLOT_SIZE,SLOT_SIZE,window,
                ID_SLOT_BASE+position,FontKind::card
            );
            preset_labels[position]=create_control(
                L"STATIC",L"和弦 · 41-60",
                WS_CHILD|WS_VISIBLE|SS_CENTER|SS_NOTIFY,
                x-35,870,195,28,window,0,FontKind::hint
            );
        }

        generate_button=create_control(
            L"BUTTON",L"生成",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            690,32,120,52,window,ID_GENERATE
        );

        update_slot_ui();
        apply_layout(window);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        int position=piano_header_at_client_point(window,point);
        if (position>=0) {
            begin_chord_edit(window,position);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (editing_chord>=0) {
            commit_chord_editor(false);
        }
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        if (begin_midi_note_drag(window,point)) {
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (dragged_midi_position>=0) {
            POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
            update_midi_note_drag(window,point);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (dragged_midi_position>=0) {
            POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
            end_midi_note_drag(window,point);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (dragged_midi_position>=0) {
            dragged_midi_position=-1;
            dragged_midi_pitch=-1;
        }
        return 0;

    case WM_CLEAR_CHORD_HOVER: {
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        clear_chord_at_client_point(window,point);
        return 0;
    }

    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;

    case WM_ENTERSIZEMOVE:
        commit_chord_editor(false);
        interactive_resize=true;
        main_background_dirty=true;
        return 0;

    case WM_SIZE:
        if (w_param!=SIZE_MINIMIZED && !interactive_resize && !controls.empty()) {
            apply_layout(window);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        interactive_resize=false;
        main_background_dirty=true;
        if (!controls.empty()) {
            apply_layout(window);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(l_param);
        RECT desired={0,0,MIN_CLIENT_SIZE,MIN_CLIENT_SIZE};
        DWORD style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
        DWORD ex_style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
        AdjustWindowRectEx(&desired,style,FALSE,ex_style);
        info->ptMinTrackSize.x=desired.right-desired.left;
        info->ptMinTrackSize.y=desired.bottom-desired.top;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint_window(window);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(w_param);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(250,250,255));
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }

    case WM_CTLCOLOREDIT: {
        HDC dc=reinterpret_cast<HDC>(w_param);
        SetBkColor(dc,RGB(250,250,253));
        SetTextColor(dc,RGB(30,34,45));
        static HBRUSH edit_background=CreateSolidBrush(RGB(250,250,253));
        return reinterpret_cast<LRESULT>(edit_background);
    }

    case WM_DRAWITEM: {
        auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (item && item->CtlType==ODT_BUTTON) {
            draw_square_button(item);
            return TRUE;
        }
        break;
    }

    case WM_CONTEXTMENU: {
        HWND source=reinterpret_cast<HWND>(w_param);
        for (int position=0;position<4;++position) {
            if (source==preset_labels[position] || source==slot_buttons[position]) {
                active_slot=position;
                slot_presets[position]=-1;
                update_slot_ui();
                return 0;
            }
        }
        break;
    }

    case WM_COMMAND: {
        int id=LOWORD(w_param);
        int notification=HIWORD(w_param);
        if (editing_chord>=0 &&
            reinterpret_cast<HWND>(l_param)!=chord_editor &&
            !commit_chord_editor(true)) {
            return 0;
        }
        if (id>=ID_CHORD_HEADER_BASE && id<ID_CHORD_HEADER_BASE+4 &&
            notification==STN_DBLCLK) {
            begin_chord_edit(window,id-ID_CHORD_HEADER_BASE);
            return 0;
        }
        if (id>=ID_SLOT_BASE && id<ID_SLOT_BASE+4 && notification==BN_CLICKED) {
            active_slot=id-ID_SLOT_BASE;
            update_slot_ui();
            return 0;
        }
        if (id>=ID_PRESET_BASE && id<ID_PRESET_BASE+5 && notification==BN_CLICKED) {
            slot_presets[active_slot]=id-ID_PRESET_BASE;
            update_slot_ui();
            return 0;
        }
        if (id==ID_GENERATE && notification==BN_CLICKED) {
            generate_and_show(window);
            return 0;
        }
        if (id==ID_SETTINGS && notification==BN_CLICKED) {
            open_configuration_window(window);
            return 0;
        }
        break;
    }

    case WM_DESTROY:
        stop_midi_playback();
        destroy_main_background_buffer();
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

    Gdiplus::GdiplusStartupInput gdiplus_input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&gdiplus_input,nullptr)!=Gdiplus::Ok) {
        MessageBoxW(nullptr,L"无法初始化图片组件。",L"启动失败",MB_OK|MB_ICONERROR);
        return 1;
    }

    background_path=executable_directory()+L"\\assets\\bg.png";
    progression_config_path=wide_to_utf8(executable_directory()+L"\\config.json");
    if (!ncnl::load_progression_config(progression_config_path)) {
        ncnl::set_progression_weights(ncnl::default_progression_weights());
        ncnl::save_progression_config(progression_config_path);
    }
    load_background(background_path);
    load_interface_images();

    const wchar_t CLASS_NAME[]=L"NoChordNoLifeGeneratorWindow";
    WNDCLASSW window_class{};
    window_class.style=CS_DBLCLKS;
    window_class.lpfnWndProc=window_procedure;
    window_class.hInstance=instance;
    window_class.lpszClassName=CLASS_NAME;
    window_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    window_class.hbrBackground=nullptr;

    if (!RegisterClassW(&window_class)) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    WNDCLASSW configuration_class{};
    configuration_class.style=CS_DBLCLKS;
    configuration_class.lpfnWndProc=configuration_window_procedure;
    configuration_class.hInstance=instance;
    configuration_class.lpszClassName=L"NoChordNoLifeConfigurationWindow";
    configuration_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    configuration_class.hbrBackground=nullptr;
    if (!RegisterClassW(&configuration_class)) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    DWORD style=WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX;
    DWORD ex_style=0;
    RECT initial_size={0,0,DESIGN_WIDTH,DESIGN_HEIGHT};
    AdjustWindowRectEx(&initial_size,style,FALSE,ex_style);

    HWND window=CreateWindowExW(
        ex_style,CLASS_NAME,L"NoChordNoLife 和弦进行生成器",
        style,
        CW_USEDEFAULT,CW_USEDEFAULT,
        initial_size.right-initial_size.left,
        initial_size.bottom-initial_size.top,
        nullptr,nullptr,instance,nullptr
    );
    if (!window) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    ShowWindow(window,show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message,nullptr,0,0)>0) {
        bool first_key_press=(message.lParam&(1LL<<30))==0;
        bool editing=message.hwnd==chord_editor || GetFocus()==chord_editor;
        bool main_window_key=GetAncestor(message.hwnd,GA_ROOT)==main_window;
        if (main_window_key &&
            (message.message==WM_RBUTTONDOWN || message.message==WM_MOUSEMOVE) &&
            (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0) {
            POINT point=message.pt;
            ScreenToClient(main_window,&point);
            SendMessageW(
                main_window,WM_CLEAR_CHORD_HOVER,0,
                MAKELPARAM(point.x,point.y)
            );
        }
        if (message.message==WM_KEYDOWN && first_key_press && !editing &&
            main_window_key) {
            if (message.wParam==VK_SPACE) {
                toggle_midi_playback(window);
                continue;
            }
            if (message.wParam==VK_RETURN) {
                SendMessageW(
                    window,WM_COMMAND,MAKEWPARAM(ID_GENERATE,BN_CLICKED),
                    reinterpret_cast<LPARAM>(generate_button)
                );
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    background_image.reset();
    arrow_image.reset();
    pentagon_image.reset();
    for (auto& image:emotion_images) {
        image.reset();
    }
    if (title_font) {
        DeleteObject(title_font);
    }
    if (normal_font) {
        DeleteObject(normal_font);
    }
    if (card_font) {
        DeleteObject(card_font);
    }
    if (hint_font) {
        DeleteObject(hint_font);
    }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return static_cast<int>(message.wParam);
}
