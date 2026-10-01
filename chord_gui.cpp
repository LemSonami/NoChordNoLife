#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <shellapi.h>

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
#include <fstream>
#include <iterator>
#include <set>

#include "chord_algorithms.hpp"
#include "chord_generator.hpp"
#include "progression_config.hpp"
#include "piano_roll_notes.hpp"
#include "midi_rhythm.hpp"

int BPM=120;

namespace {

constexpr int ID_GENERATE=1003;
constexpr int ID_SETTINGS=1004;
constexpr int ID_PRESET_ACTION=1005;
constexpr int ID_IMPORT_MIDI=1006;
constexpr int ID_EXPORT_MIDI=1007;
constexpr UINT_PTR PLAYBACK_TIMER=2;
constexpr UINT_PTR DISSOLVE_TIMER=3;
constexpr UINT_PTR KEY_PREVIEW_TIMER=4;
constexpr int ID_PRESET_BASE=1100;
constexpr UINT WM_CLEAR_CHORD_HOVER=WM_APP+1;
constexpr UINT WM_ANIMATION_FRAME=WM_APP+2;
constexpr double TARGET_ANIMATION_FPS=120.0;

constexpr int DESIGN_WIDTH=1000;
constexpr int DESIGN_HEIGHT=1000;
constexpr int MIN_CLIENT_SIZE=600;
constexpr int SETTINGS_DESIGN_SIZE=800;
constexpr int SLOT_Y=735;

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
std::vector<int> slot_presets(4,2);
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
HDC animation_dc=nullptr;
HBITMAP animation_bitmap=nullptr;
HGDIOBJ animation_old_bitmap=nullptr;

ULONG_PTR gdiplus_token=0;
std::unique_ptr<Gdiplus::PrivateFontCollection> interface_font_collection;
std::unique_ptr<Gdiplus::FontFamily> interface_font_family;
std::wstring interface_font_name=L"Microsoft YaHei UI";
std::wstring registered_font_path;
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
ncnl::GeneratedProgression displayed_progression{
    std::vector<ncnl::GeneratedChord>(4),0.0
};
ncnl::MidiRhythm rhythm{
};
std::vector<bool> rhythm_splits(3,true);
struct ChordBlock { std::size_t first,last; }; // last 是不包含的右端事件索引。
std::vector<ChordBlock> chord_blocks;
int dragged_midi_event=-1;
int roll_low_pitch=60;
int roll_high_pitch=71;
ULONGLONG playback_start_ms=0;
double playback_loop_ms=0.0;
struct DissolvingNote {
    Gdiplus::RectF bounds; // Logical coordinates: scale only when rendering.
    ULONGLONG born;
    unsigned seed;
    bool alternate;
};
std::vector<DissolvingNote> dissolving_notes;
constexpr ULONGLONG DISSOLVE_DURATION_MS=760;
std::wstring rhythm_file_name;
int editing_chord=-1;
int dragged_midi_position=-1;
int dragged_midi_pitch=-1;
HANDLE midi_playback_thread=nullptr;
HANDLE midi_stop_event=nullptr;
HMIDIOUT shared_midi_output=nullptr;
int key_preview_pitch=-1;
bool key_preview_gesture=false;
MMRESULT animation_clock=0;
bool animation_period_active=false;
volatile LONG animation_frame_pending=0;
double animation_next_frame_ms=0;
void update_animation_clock();
ULONGLONG key_preview_started=0;
void stop_key_preview();

ULONGLONG monotonic_ms() {
    static const LONGLONG frequency=[](){
        LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value.QuadPart;
    }();
    LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter);
    return static_cast<ULONGLONG>(counter.QuadPart/static_cast<double>(frequency)*1000.0);
}

bool wait_midi_deadline(HANDLE stop,double deadline) {
    for (;;) {
        double remaining=deadline-monotonic_ms();
        DWORD delay=static_cast<DWORD>(std::ceil(std::max(0.0,std::min(60000.0,remaining))));
        if (WaitForSingleObject(stop,delay)==WAIT_OBJECT_0) { return true; }
        if (remaining<=60000.0) { return false; }
    }
}

void initialize_rhythm() {
    rhythm.events.clear();
    for (int i=0;i<4;++i) {
        rhythm.events.push_back({static_cast<double>(i),0.9,92,{}});
    }
    rhythm.length=4.0;
    chord_blocks={{0,1},{1,2},{2,3},{3,4}};
}

std::size_t event_block(std::size_t event) {
    for (std::size_t i=0;i<chord_blocks.size();++i) {
        if (event>=chord_blocks[i].first && event<chord_blocks[i].last) { return i; }
    }
    return 0;
}

double event_boundary(std::size_t right_event) {
    const auto& left=rhythm.events[right_event-1];
    return (left.start+left.duration+rhythm.events[right_event].start)/2.0;
}

double block_start(std::size_t block) {
    return block==0 ? 0.0 : event_boundary(chord_blocks[block].first);
}

double block_end(std::size_t block) {
    return block+1==chord_blocks.size() ? rhythm.length
        : event_boundary(chord_blocks[block].last);
}

double timeline_x(double beat) { return 132.0+818.0*beat/rhythm.length; }

Gdiplus::RectF arrow_bounds(std::size_t right_event) {
    float size=std::min(42.0f,818.0f/static_cast<float>(chord_blocks.size()));
    return Gdiplus::RectF(static_cast<float>(timeline_x(event_boundary(right_event)))-size/2,
        658,size,size);
}

Gdiplus::RectF visual_note_bounds(const ncnl::RhythmEvent& event,int pitch) {
    float row=298.0f/(roll_high_pitch-roll_low_pitch+1);
    float padding=std::min(2.0f,row*0.15f);
    return Gdiplus::RectF(static_cast<float>(timeline_x(event.start)+1.0),
        347.0f+(roll_high_pitch-pitch)*row+padding,
        std::max(2.0f,static_cast<float>(818.0*event.duration/rhythm.length-2.0)),
        std::max(1.0f,row-2.0f*padding));
}

void start_note_dissolve(std::size_t event,int pitch) {
    // Bound the animation cost even when a long imported block is erased at once.
    if (dissolving_notes.size()>=64) { dissolving_notes.erase(dissolving_notes.begin()); }
    auto born=monotonic_ms();
    dissolving_notes.push_back({visual_note_bounds(rhythm.events[event],pitch),born,
        static_cast<unsigned>(event*137+pitch*73+born),event_block(event)%2!=0});
    update_animation_clock();
}

void expire_dissolving_notes(ULONGLONG now) {
    dissolving_notes.erase(std::remove_if(dissolving_notes.begin(),dissolving_notes.end(),
        [now](const DissolvingNote& note){ return now-note.born>=DISSOLVE_DURATION_MS; }),
        dissolving_notes.end());
    update_animation_clock();
}

void discard_note_effects() {
    dissolving_notes.clear();
    update_animation_clock();
}

void CALLBACK animation_clock_callback(UINT,UINT,DWORD_PTR target,DWORD_PTR,DWORD_PTR) {
    double now=static_cast<double>(monotonic_ms());
    if (now<animation_next_frame_ms) { return; }
    animation_next_frame_ms=std::max(animation_next_frame_ms+1000.0/TARGET_ANIMATION_FPS,
        now+1.0);
    if (InterlockedCompareExchange(&animation_frame_pending,1,0)==0) {
        if (!PostMessageW(reinterpret_cast<HWND>(target),WM_ANIMATION_FRAME,0,0)) {
            InterlockedExchange(&animation_frame_pending,0);
        }
    }
}

void stop_animation_clock() {
    if (animation_clock) { timeKillEvent(animation_clock); animation_clock=0; }
    KillTimer(main_window,PLAYBACK_TIMER); // fallback timer, if the multimedia timer failed
    if (animation_period_active) { timeEndPeriod(1); animation_period_active=false; }
    InterlockedExchange(&animation_frame_pending,0);
}

void update_animation_clock() {
    bool needed=main_window && IsWindow(main_window) && !IsIconic(main_window) &&
        !interactive_resize && (playback_loop_ms>0 || !dissolving_notes.empty());
    if (!needed) { stop_animation_clock(); return; }
    if (animation_clock || animation_period_active) { return; }
    animation_period_active=timeBeginPeriod(1)==TIMERR_NOERROR;
    animation_next_frame_ms=static_cast<double>(monotonic_ms());
    animation_clock=timeSetEvent(1,1,animation_clock_callback,
        reinterpret_cast<DWORD_PTR>(main_window),TIME_PERIODIC|TIME_KILL_SYNCHRONOUS);
    if (!animation_clock) {
        if (animation_period_active) { timeEndPeriod(1); animation_period_active=false; }
        SetTimer(main_window,PLAYBACK_TIMER,10,nullptr);
    }
}

Gdiplus::RectF emotion_bounds(std::size_t block) {
    float left=static_cast<float>(timeline_x(block_start(block)));
    float right=static_cast<float>(timeline_x(block_end(block)));
    float cell=right-left;
    float gap=std::min(16.0f,cell*0.15f);
    float side=std::max(0.5f,std::min(125.0f,cell-gap));
    return Gdiplus::RectF((left+right-side)/2.0f,
        static_cast<float>(SLOT_Y),side,side);
}

void update_pitch_range() {
    roll_low_pitch=60; roll_high_pitch=71;
    for (const auto& event:rhythm.events) {
        for (int pitch:event.pitches) {
            roll_low_pitch=std::min(roll_low_pitch,pitch);
            roll_high_pitch=std::max(roll_high_pitch,pitch);
        }
    }
}

void apply_block_notes(std::size_t block) {
    auto pcs=ncnl::parse_roll_notes(displayed_progression.chords[block].notes);
    for (std::size_t event=chord_blocks[block].first;event<chord_blocks[block].last;++event) {
        rhythm.events[event].pitches.clear();
        for (int pc:pcs) { rhythm.events[event].pitches.push_back(60+pc); }
    }
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

void unload_interface_font() {
    interface_font_family.reset();
    interface_font_collection.reset();
    if (!registered_font_path.empty()) {
        RemoveFontResourceExW(registered_font_path.c_str(),FR_PRIVATE,nullptr);
        registered_font_path.clear();
    }
    interface_font_name=L"Microsoft YaHei UI";
}

bool load_interface_font(const std::wstring& path) {
    unload_interface_font();
    std::unique_ptr<Gdiplus::PrivateFontCollection> collection(new Gdiplus::PrivateFontCollection);
    if (collection->AddFontFile(path.c_str())!=Gdiplus::Ok) { return false; }
    Gdiplus::FontFamily family;
    INT found=0;
    if (collection->GetFamilies(1,&family,&found)!=Gdiplus::Ok || !found) { return false; }
    wchar_t name[LF_FACESIZE]={};
    if (family.GetFamilyName(name)!=Gdiplus::Ok ||
        !AddFontResourceExW(path.c_str(),FR_PRIVATE,nullptr)) { return false; }
    registered_font_path=path;
    interface_font_name=name;
    interface_font_family.reset(family.Clone());
    interface_font_collection=std::move(collection);
    return interface_font_family!=nullptr;
}

HFONT create_scaled_font(int logical_height,int weight,double scale) {
    int height=-std::max(9,static_cast<int>(std::lround(logical_height*scale)));
    return CreateFontW(
        height,0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,interface_font_name.c_str()
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
    main_background_dirty=true;
    InvalidateRect(main_window,nullptr,FALSE);
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
    for (std::size_t position=0;position<chord_blocks.size();++position) {
        auto bounds=emotion_bounds(position);
        if (logical_x>=bounds.X && logical_x<=bounds.GetRight() &&
            logical_y>=bounds.Y && logical_y<=bounds.GetBottom()) {
            return static_cast<int>(position);
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

bool is_black_key(int pitch) {
    int pc=pitch%12;
    return pc==1 || pc==3 || pc==6 || pc==8 || pc==10;
}

Gdiplus::RectF keyboard_key_bounds(int pitch) {
    float row=298.0f/(roll_high_pitch-roll_low_pitch+1);
    float center=347.0f+(roll_high_pitch-pitch+0.5f)*row;
    if (is_black_key(pitch)) {
        return Gdiplus::RectF(50,center-row*0.44f,53,row*0.88f);
    }
    int above=pitch+1,below=pitch-1;
    while (above<=127 && is_black_key(above)) { ++above; }
    while (below>=0 && is_black_key(below)) { --below; }
    float top=std::max(347.0f,center-(above-pitch)*row*0.5f);
    float bottom=std::min(645.0f,center+(pitch-below)*row*0.5f);
    return Gdiplus::RectF(50,top,82,std::max(0.0f,bottom-top));
}

int keyboard_pitch_at_client_point(HWND window,POINT point) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    if (x<50 || x>=132 || y<347 || y>=645) { return -1; }
    // Overlay black keys take precedence; the exposed right-hand area is a white key.
    for (int pitch=roll_low_pitch;pitch<=roll_high_pitch;++pitch) {
        if (is_black_key(pitch) && keyboard_key_bounds(pitch).Contains(
            static_cast<float>(x),static_cast<float>(y))) { return pitch; }
    }
    for (int pitch=std::max(0,roll_low_pitch-2);pitch<=std::min(127,roll_high_pitch+2);++pitch) {
        if (!is_black_key(pitch) && keyboard_key_bounds(pitch).Contains(
            static_cast<float>(x),static_cast<float>(y))) { return pitch; }
    }
    return -1;
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
    double beat=(logical_x-132.0)/818.0*rhythm.length;
    for (std::size_t block=0;block<chord_blocks.size();++block) {
        if (beat>=block_start(block) && beat<=block_end(block)) {
            return static_cast<int>(block);
        }
    }
    return -1;
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
    if (logical_x<grid_left || logical_x>950.0 ||
        logical_y<grid_top || logical_y>=grid_bottom) {
        return false;
    }
    int rows=roll_high_pitch-roll_low_pitch+1;
    double row_height=(grid_bottom-grid_top)/rows;
    int lane=std::max(0,std::min(
        rows-1,static_cast<int>((logical_y-grid_top)/row_height)
    ));
    double padding=std::min(2.0,row_height*0.15);
    double within_lane=logical_y-grid_top-lane*row_height;
    if (within_lane<padding || within_lane>row_height-padding) {
        return false;
    }
    pitch=roll_high_pitch-lane;
    for (std::size_t i=0;i<rhythm.events.size();++i) {
        const auto& event=rhythm.events[i];
        double left=timeline_x(event.start)+1.0;
        double right=std::max(left+2.0,timeline_x(event.start+event.duration)-1.0);
        if (logical_x>=left && logical_x<=right) {
            position=static_cast<int>(i);
            return true;
        }
    }
    return false;
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
    auto bounds=emotion_bounds(static_cast<std::size_t>(position));
    RECT result={
        static_cast<LONG>(std::lround(offset_x+bounds.X*scale)),
        static_cast<LONG>(std::lround(offset_y+bounds.Y*scale)),
        static_cast<LONG>(std::lround(offset_x+bounds.GetRight()*scale)),
        static_cast<LONG>(std::lround(offset_y+bounds.GetBottom()*scale))
    };
    return result;
}

void toggle_drag_highlight(int position) {
    if (position<0 || position>=static_cast<int>(chord_blocks.size()) || !main_window) {
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
    ncnl::MidiRhythm rhythm;
    ULONGLONG start_ms;
};

struct MidiOutputApi {
    decltype(&midiOutOpen) open=&midiOutOpen;
    decltype(&midiOutClose) close=&midiOutClose;
    decltype(&midiOutReset) reset=&midiOutReset;
    decltype(&midiOutShortMsg) message=&midiOutShortMsg;
    decltype(&midiOutGetNumDevs) count=&midiOutGetNumDevs;
} midi_output_api;

struct MidiMessageLock {
    CRITICAL_SECTION section;
    MidiMessageLock() { InitializeCriticalSection(&section); }
    ~MidiMessageLock() { DeleteCriticalSection(&section); }
} midi_message_lock;

void send_midi_message(HMIDIOUT output,DWORD message) {
    EnterCriticalSection(&midi_message_lock.section);
    midi_output_api.message(output,message);
    LeaveCriticalSection(&midi_message_lock.section);
}

MMRESULT open_shared_midi_output() {
    if (shared_midi_output) { return MMSYSERR_NOERROR; }
    HMIDIOUT opened=nullptr;
    MMRESULT result=midi_output_api.open(&opened,MIDI_MAPPER,0,0,CALLBACK_NULL);
    if (result!=MMSYSERR_NOERROR) {
        // A missing/broken mapper does not necessarily mean there is no usable device.
        UINT count=midi_output_api.count();
        for (UINT id=0;id<count && result!=MMSYSERR_NOERROR;++id) {
            opened=nullptr;
            result=midi_output_api.open(&opened,id,0,0,CALLBACK_NULL);
        }
    }
    if (result==MMSYSERR_NOERROR) {
        shared_midi_output=opened;
        send_midi_message(opened,0xC0u); // progression: channel 0, piano
        send_midi_message(opened,0xC1u); // audition: channel 1, piano
    }
    return result;
}

void show_midi_open_error(HWND owner,MMRESULT error) {
    wchar_t detail[256]={};
    midiOutGetErrorTextW(error,detail,256);
    std::wostringstream message;
    message<<L"无法打开 Windows MIDI 播放设备。\n错误码："<<error<<L"\n"<<detail;
    MessageBoxW(owner,message.str().c_str(),L"无法播放",MB_OK|MB_ICONERROR);
}

void invalidate_piano_keys() {
    main_background_dirty=true;
    if (main_window) {
        RECT area={static_cast<LONG>(50*current_scale),static_cast<LONG>(347*current_scale),
            static_cast<LONG>(133*current_scale),static_cast<LONG>(646*current_scale)};
        InvalidateRect(main_window,&area,FALSE);
    }
}

void send_midi_note(HMIDIOUT output,int note,int velocity,bool note_on,int channel=0) {
    DWORD status=(note_on ? 0x90u : 0x80u)|static_cast<DWORD>(channel&15);
    DWORD message=status |
        (static_cast<DWORD>(note&0x7F)<<8) |
        (static_cast<DWORD>(velocity&0x7F)<<16);
    send_midi_message(output,message);
}

void stop_key_preview() {
    KillTimer(main_window,KEY_PREVIEW_TIMER);
    if (shared_midi_output && key_preview_pitch>=0) {
        send_midi_note(shared_midi_output,key_preview_pitch,0,false,1);
    }
    bool was_active=key_preview_pitch>=0;
    key_preview_pitch=-1;
    if (was_active) { invalidate_piano_keys(); }
}

bool play_key_preview(HWND owner,int pitch) {
    if (pitch<0 || pitch>127) { return false; }
    MMRESULT opened=open_shared_midi_output();
    if (opened!=MMSYSERR_NOERROR) {
        show_midi_open_error(owner,opened);
        return false;
    }
    stop_key_preview();
    key_preview_pitch=pitch;
    key_preview_started=monotonic_ms();
    send_midi_note(shared_midi_output,pitch,96,true,1);
    invalidate_piano_keys();
    return true;
}

void release_key_preview() {
    ULONGLONG elapsed=monotonic_ms()-key_preview_started;
    if (elapsed>=160) { stop_key_preview(); }
    else { SetTimer(main_window,KEY_PREVIEW_TIMER,static_cast<UINT>(160-elapsed),nullptr); }
}

DWORD WINAPI midi_playback_procedure(LPVOID parameter) {
    std::unique_ptr<MidiPlaybackData> data(
        static_cast<MidiPlaybackData*>(parameter)
    );
    // General MIDI program 0：Acoustic Grand Piano。
    send_midi_message(data->output,0xC0u);
    struct ScheduledNote { double beat; int pitch,velocity; bool on; };
    std::vector<ScheduledNote> schedule;
    for (const auto& event:data->rhythm.events) {
        for (int pitch:event.pitches) {
            schedule.push_back({event.start,pitch,event.velocity,true});
            schedule.push_back({event.start+event.duration,pitch,0,false});
        }
    }
    std::sort(schedule.begin(),schedule.end(),[](const ScheduledNote& a,const ScheduledNote& b){
        return a.beat!=b.beat ? a.beat<b.beat : a.on<b.on;
    });
    double beat_ms=60000.0/std::max(1,data->bpm);
    double loop_ms=data->rhythm.length*beat_ms;
    std::uint64_t loop=0;
    bool stopping=false;
    while (!stopping) {
        for (const auto& note:schedule) {
            double deadline=data->start_ms+loop*loop_ms+note.beat*beat_ms;
            stopping=wait_midi_deadline(data->stop_event,deadline);
            if (stopping) { break; }
            send_midi_note(data->output,note.pitch,note.velocity,note.on);
        }
        if (!stopping) {
            stopping=wait_midi_deadline(data->stop_event,data->start_ms+(loop+1)*loop_ms);
        }
        ++loop;
    }
    // Stop only the progression channel; keep channel 1 and the shared port alive.
    send_midi_message(data->output,0xB0u|(64u<<8));
    send_midi_message(data->output,0xB0u|(123u<<8));
    send_midi_message(data->output,0xB0u|(120u<<8));
    return 0;
}

void stop_midi_playback() {
    if (main_window) { KillTimer(main_window,PLAYBACK_TIMER); }
    playback_loop_ms=0.0;
    update_animation_clock();
    if (main_window) { InvalidateRect(main_window,nullptr,FALSE); }
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
    bool has_notes=false;
    for (const auto& event:rhythm.events) { has_notes=has_notes || !event.pitches.empty(); }
    if (!has_notes) {
        MessageBoxW(owner,L"请先导入 MIDI 或生成和弦。",L"无法播放",MB_OK|MB_ICONINFORMATION);
        return;
    }
    data->rhythm=rhythm;

    MMRESULT opened=open_shared_midi_output();
    if (opened!=MMSYSERR_NOERROR) {
        show_midi_open_error(owner,opened);
        return;
    }
    data->output=shared_midi_output;

    midi_stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!midi_stop_event) {
        MessageBoxW(owner,L"无法创建播放事件。",L"无法播放",MB_OK|MB_ICONERROR);
        return;
    }
    data->stop_event=midi_stop_event;
    data->bpm=BPM;
    data->start_ms=monotonic_ms();
    playback_start_ms=data->start_ms;
    midi_playback_thread=CreateThread(
        nullptr,0,midi_playback_procedure,data.get(),0,nullptr
    );
    if (!midi_playback_thread) {
        CloseHandle(midi_stop_event);
        midi_stop_event=nullptr;
        MessageBoxW(owner,L"无法创建播放线程。",L"无法播放",MB_OK|MB_ICONERROR);
        return;
    }
    playback_loop_ms=rhythm.length*60000.0/std::max(1,BPM);
    update_animation_clock();
    data.release();
}

void generate_and_show(HWND owner) {
    try {
        stop_midi_playback();
        discard_note_effects();
        std::vector<ncnl::ChordConstraint> constraints(chord_blocks.size());
        bool all_chords_filled=true;
        for (const auto& chord:displayed_progression.chords) {
            all_chords_filled=all_chords_filled && !chord.notes.empty();
        }
        for (std::size_t position=0;position<chord_blocks.size();++position) {
            constraints[position].emotion_preset=slot_presets[position];
            // 只重新生成已清空的位置；如果所有分块都有和弦，则把这次操作
            // 解释为“全部重新生成”。
            constraints[position].fixed_notes=all_chords_filled
                ? ""
                : displayed_progression.chords[position].notes;
        }

        displayed_progression=ncnl::generate_progression(
            current_mode,constraints
        );
        for (std::size_t position=0;position<chord_blocks.size();++position) {
            if (constraints[position].fixed_notes.empty()) { apply_block_notes(position); }
        }
        update_pitch_range();
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
    Gdiplus::FontFamily fallback(L"Microsoft YaHei UI");
    Gdiplus::FontFamily* family=interface_font_family ? interface_font_family.get() : &fallback;
    INT style=bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
    if (!family->IsStyleAvailable(style)) { style=Gdiplus::FontStyleRegular; }
    Gdiplus::Font font(
        family,size,style,
        Gdiplus::UnitPixel
    );
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(text.c_str(),-1,&font,bounds,&format,&brush);
}

bool progression_is_complete() {
    if (displayed_progression.chords.empty()) { return false; }
    for (const auto& chord:displayed_progression.chords) {
        if (ncnl::parse_roll_notes(chord.notes).size()<2) {
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
    std::size_t count=displayed_progression.chords.size();
    for (std::size_t position=0;position<count;++position) {
        std::size_t next_position=(position+1)%count;
        const std::string& current=displayed_progression.chords[position].notes;
        const std::string& next=
            displayed_progression.chords[next_position].notes;
        total+=ncnl::chord_progression_score(current_mode,current,next);
        repeated+=count>1 && chord_pitch_mask(current)==chord_pitch_mask(next);
    }
    displayed_progression.quality_score=total/count-56.0*repeated/count;
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

void sync_block_from_events(std::size_t block) {
    ncnl::Chord pitches;
    for (std::size_t event=chord_blocks[block].first;event<chord_blocks[block].last;++event) {
        for (int pitch:rhythm.events[event].pitches) {
            int pc=pitch%12;
            if (std::find(pitches.begin(),pitches.end(),pc)==pitches.end()) { pitches.push_back(pc); }
        }
    }
    std::string notes=pitch_classes_to_text(pitches);
    displayed_progression.chords[block]={notes,ncnl::roll_emotion_score(current_mode,notes)};
}

bool create_midi_note_at_client_point(HWND window,POINT point) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    if (x<132 || x>=950 || y<347 || y>=645) { return false; }
    int rows=roll_high_pitch-roll_low_pitch+1;
    int pitch=roll_high_pitch-std::min(rows-1,static_cast<int>((y-347)/(298.0/rows)));
    double beat=(x-132)/818*rhythm.length;
    // A blank pitch lane at an existing attack inherits that attack's exact timing.
    for (std::size_t i=0;i<rhythm.events.size();++i) {
        auto& event=rhythm.events[i];
        if (beat>=event.start && beat<event.start+event.duration) {
            if (std::find(event.pitches.begin(),event.pitches.end(),pitch)==event.pitches.end()) {
                stop_midi_playback();
                event.pitches.push_back(pitch);
                active_slot=static_cast<int>(event_block(i));
                sync_block_from_events(event_block(i));
                refresh_progression_display();
            }
            return true;
        }
    }
    // Rest-area clicks add a sixteenth-note attack, clipped to its neighbours.
    std::size_t insertion=0;
    while (insertion<rhythm.events.size() && rhythm.events[insertion].start<beat) { ++insertion; }
    double previous_end=insertion ? rhythm.events[insertion-1].start+rhythm.events[insertion-1].duration : 0;
    double next_start=insertion<rhythm.events.size() ? rhythm.events[insertion].start : rhythm.length;
    double start=std::max(previous_end,std::floor(beat*4.0)/4.0);
    double end=std::min(next_start,start+0.25);
    if (end<=start) { return false; }
    std::size_t block=0;
    while (block+1<chord_blocks.size() && beat>=block_end(block)) { ++block; }
    stop_midi_playback();
    discard_note_effects();
    rhythm.events.insert(rhythm.events.begin()+insertion,{start,end-start,96,{pitch}});
    // Retain all existing group settings and assign the new attack to its time region.
    ++chord_blocks[block].last;
    for (std::size_t i=block+1;i<chord_blocks.size();++i) {
        ++chord_blocks[i].first; ++chord_blocks[i].last;
    }
    rhythm_splits.assign(rhythm.events.size()-1,false);
    for (std::size_t i=0;i+1<chord_blocks.size();++i) {
        rhythm_splits[chord_blocks[i].last-1]=true;
    }
    active_slot=static_cast<int>(block);
    sync_block_from_events(block);
    refresh_progression_display();
    return true;
}

void merge_rhythm_boundary(std::size_t boundary);

void clear_chord_at_client_point(HWND window,POINT point) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    for (std::size_t i=0;i<rhythm_splits.size();++i) {
        if (rhythm_splits[i] && arrow_bounds(i+1).Contains(static_cast<float>(x),static_cast<float>(y))) {
            merge_rhythm_boundary(i);
            return;
        }
    }
    int position=slot_at_client_point(window,point);
    if (position>=0) {
        if (slot_presets[position]!=-1) {
            active_slot=position;
            slot_presets[position]=-1;
            update_slot_ui();
        }
        return;
    }

    position=piano_header_at_client_point(window,point);
    if (position>=0) {
        bool has_events=false;
        const auto& block=chord_blocks[static_cast<std::size_t>(position)];
        for (std::size_t i=block.first;i<block.last;++i) {
            has_events=has_events || !rhythm.events[i].pitches.empty();
        }
        if (!displayed_progression.chords[position].notes.empty() ||
            editing_chord==position || has_events) {
            stop_midi_playback();
            hide_chord_editor();
            for (std::size_t i=block.first;i<block.last;++i) {
                for (int pitch:rhythm.events[i].pitches) { start_note_dissolve(i,pitch); }
            }
            displayed_progression.chords[position]={"",0.0};
            apply_block_notes(static_cast<std::size_t>(position));
            refresh_progression_display();
        }
        return;
    }

    int pitch=-1;
    if (!piano_note_at_client_point(window,point,position,pitch)) {
        return;
    }
    auto& event=rhythm.events[static_cast<std::size_t>(position)];
    auto& notes=event.pitches;
    auto note=std::find(notes.begin(),notes.end(),pitch);
    if (note==notes.end()) {
        return;
    }
    stop_midi_playback();
    hide_chord_editor();
    start_note_dissolve(static_cast<std::size_t>(position),pitch);
    notes.erase(note);
    sync_block_from_events(event_block(static_cast<std::size_t>(position)));
    refresh_progression_display();
}

bool begin_midi_note_drag(HWND window,POINT point) {
    int position=-1;
    int pitch=-1;
    if (!piano_note_at_client_point(window,point,position,pitch)) {
        return false;
    }
    const auto& chord=rhythm.events[static_cast<std::size_t>(position)].pitches;
    if (std::find(chord.begin(),chord.end(),pitch)==chord.end()) {
        return false;
    }
    dragged_midi_position=static_cast<int>(event_block(static_cast<std::size_t>(position)));
    dragged_midi_event=position;
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
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    int rows=roll_high_pitch-roll_low_pitch+1;
    int lane=std::max(0,std::min(rows-1,static_cast<int>((y-347.0)/(298.0/rows))));
    int target_pitch=roll_high_pitch-lane;
    if (target_pitch==dragged_midi_pitch) {
        return;
    }

    auto& chord=rhythm.events[static_cast<std::size_t>(dragged_midi_event)].pitches;
    if (std::find(chord.begin(),chord.end(),target_pitch)!=chord.end()) {
        return;
    }
    auto source=std::find(chord.begin(),chord.end(),dragged_midi_pitch);
    if (source==chord.end()) {
        return;
    }
    *source=target_pitch;
    sync_block_from_events(static_cast<std::size_t>(dragged_midi_position));
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
    dragged_midi_event=-1;
    dragged_midi_pitch=-1;
    if (GetCapture()==window) {
        ReleaseCapture();
    }
}

void rebuild_chord_blocks() {
    std::vector<ChordBlock> blocks;
    std::size_t first=0;
    for (std::size_t i=0;i<rhythm_splits.size();++i) {
        if (rhythm_splits[i]) { blocks.push_back({first,i+1}); first=i+1; }
    }
    blocks.push_back({first,rhythm.events.size()});
    std::vector<ncnl::GeneratedChord> chords;
    std::vector<int> presets;
    for (const auto& block:blocks) {
        std::size_t old=event_block(block.first);
        chords.push_back(displayed_progression.chords[old]);
        presets.push_back(slot_presets[old]);
    }
    chord_blocks=std::move(blocks);
    displayed_progression.chords=std::move(chords);
    slot_presets=std::move(presets);
    active_slot=std::min(active_slot,static_cast<int>(chord_blocks.size())-1);
}

void merge_rhythm_boundary(std::size_t boundary) {
    if (boundary>=rhythm_splits.size() || !rhythm_splits[boundary]) { return; }
    stop_midi_playback();
    hide_chord_editor();
    rhythm_splits[boundary]=false;
    rebuild_chord_blocks();
    std::size_t block=event_block(boundary);
    if (!displayed_progression.chords[block].notes.empty()) { apply_block_notes(block); }
    refresh_progression_display();
}

void toggle_rhythm_split(HWND window,POINT point) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    if (x<132.0 || x>950.0 || y<295.0 || y>725.0 || rhythm.events.size()<2) { return; }
    double nearest=15.0;
    int boundary=-1;
    for (std::size_t i=1;i<rhythm.events.size();++i) {
        double distance=std::abs(x-timeline_x(event_boundary(i)));
        if (distance<nearest) { nearest=distance; boundary=static_cast<int>(i)-1; }
    }
    if (boundary<0) { return; }
    if (rhythm_splits[static_cast<std::size_t>(boundary)]) {
        merge_rhythm_boundary(static_cast<std::size_t>(boundary));
        return;
    }
    stop_midi_playback();
    hide_chord_editor();
    rhythm_splits[static_cast<std::size_t>(boundary)]=true;
    rebuild_chord_blocks();
    refresh_progression_display();
}

std::vector<unsigned char> read_midi_file(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) { throw std::runtime_error("无法读取 MIDI 文件。"); }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>32*1024*1024) {
        CloseHandle(file); throw std::runtime_error("MIDI 文件为空或超过 32 MB。");
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read=0;
    BOOL ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr);
    CloseHandle(file);
    if (!ok || read!=bytes.size()) { throw std::runtime_error("MIDI 文件读取不完整。"); }
    return bytes;
}

void import_midi_file(HWND owner,const std::wstring& path) {
    try {
        auto imported=ncnl::parse_midi_rhythm(read_midi_file(path));
        stop_midi_playback();
        discard_note_effects();
        hide_chord_editor();
        rhythm=std::move(imported);
        rhythm_splits.assign(rhythm.events.size()-1,false);
        chord_blocks={{0,rhythm.events.size()}};
        displayed_progression.chords.assign(1,{"",0.0});
        displayed_progression.quality_score=0.0;
        slot_presets.assign(1,2);
        active_slot=0;
        drag_hover_slot=-1;
        dragged_midi_position=-1;
        dragged_midi_event=-1;
        dragged_midi_pitch=-1;
        std::size_t slash=path.find_last_of(L"\\/");
        rhythm_file_name=path.substr(slash==std::wstring::npos ? 0 : slash+1);
        update_pitch_range();
        update_slot_ui();
    }
    catch (const std::exception& error) {
        MessageBoxW(owner,utf8_to_wide(error.what()).c_str(),L"无法导入 MIDI",MB_OK|MB_ICONERROR);
    }
}

void choose_midi_file(HWND owner,bool save) {
    wchar_t path[32768]={};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=owner;
    dialog.lpstrFilter=L"MIDI 文件 (*.mid;*.midi)\0*.mid;*.midi\0\0";
    dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.lpstrDefExt=L"mid";
    dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) { return; }
    if (!save) { import_midi_file(owner,path); return; }
    std::vector<unsigned char> bytes;
    try { bytes=ncnl::encode_midi(rhythm,BPM); }
    catch (const std::exception& error) {
        MessageBoxW(owner,utf8_to_wide(error.what()).c_str(),L"无法导出 MIDI",MB_OK|MB_ICONERROR);
        return;
    }
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0;
    BOOL ok=file!=INVALID_HANDLE_VALUE && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
    if (file!=INVALID_HANDLE_VALUE) { CloseHandle(file); }
    if (!ok || written!=bytes.size()) {
        MessageBoxW(owner,L"无法保存 MIDI 文件。",L"导出失败",MB_OK|MB_ICONERROR);
    }
}

void hide_chord_editor() {
    editing_chord=-1;
    if (chord_editor) {
        ShowWindow(chord_editor,SW_HIDE);
    }
}

bool commit_chord_editor(bool show_error) {
    if (editing_chord<0 || editing_chord>=static_cast<int>(chord_blocks.size())) {
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
            const auto& block=chord_blocks[static_cast<std::size_t>(position)];
            for (std::size_t i=block.first;i<block.last;++i) {
                for (int pitch:rhythm.events[i].pitches) { start_note_dissolve(i,pitch); }
            }
            displayed_progression.chords[position]={"",0.0};
        }
        else {
            ncnl::parse_chord(notes);
            double score=ncnl::chord_emotion_score(current_mode,notes);
            stop_midi_playback();
            discard_note_effects();
            displayed_progression.chords[position]={notes,score};
        }
        apply_block_notes(static_cast<std::size_t>(position));
        update_pitch_range();
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
    if (position<0 || position>=static_cast<int>(chord_blocks.size())) {
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
    double left=timeline_x(block_start(static_cast<std::size_t>(position)));
    double section_width=timeline_x(block_end(static_cast<std::size_t>(position)))-left;
    int x=offset_x+static_cast<int>(std::lround(
        (left+2.0)*scale
    ));
    int y=offset_y+static_cast<int>(std::lround(300.0*scale));
    int editor_width=std::max(50,static_cast<int>(std::lround((section_width-4.0)*scale)));
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
    const int rows=roll_high_pitch-roll_low_pitch+1;
    const float row_height=grid_height/rows;
    const float radius=18.0f*scale;

    Gdiplus::RectF outer(left,top,roll_width,roll_height);
    Gdiplus::GraphicsPath rounded;
    add_rounded_rectangle(rounded,outer,radius);
    Gdiplus::GraphicsState state=graphics.Save();
    graphics.SetClip(&rounded);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::SolidBrush frame(Gdiplus::Color(238,22,27,34));
    Gdiplus::SolidBrush header(Gdiplus::Color(245,35,42,53));
    graphics.FillRectangle(&frame,outer);
    graphics.FillRectangle(&header,left,top,roll_width,header_height);

    for (int lane=0;lane<rows;++lane) {
        int pitch=roll_high_pitch-lane;
        int pitch_class=pitch%12;
        bool black_key=pitch_class==1 || pitch_class==3 || pitch_class==6 ||
                       pitch_class==8 || pitch_class==10;
        float y=grid_top+lane*row_height;
        Gdiplus::SolidBrush lane_brush(
            black_key ? Gdiplus::Color(240,25,29,36)
                      : Gdiplus::Color(232,43,48,58)
        );
        graphics.FillRectangle(&lane_brush,grid_left,y,grid_width,row_height);

    }

    // White keys extend beneath the overlay black keys, as on a real piano.
    // Chromatic lanes retain subtle alternating fills, without horizontal/vertical grid lines.
    Gdiplus::SolidBrush keyboard_base(Gdiplus::Color(255,226,230,236));
    graphics.FillRectangle(&keyboard_base,left,grid_top,keyboard_width,grid_height);
    for (int pass=0;pass<2;++pass) {
        for (int pitch=std::min(127,roll_high_pitch+2);pitch>=std::max(0,roll_low_pitch-2);--pitch) {
            bool black=is_black_key(pitch);
            if (black!=(pass==1) || (black && (pitch<roll_low_pitch || pitch>roll_high_pitch))) { continue; }
            auto logical=keyboard_key_bounds(pitch);
            if (logical.Height<=0) { continue; }
            Gdiplus::RectF key(offset_x+logical.X*scale,offset_y+logical.Y*scale,
                logical.Width*scale,logical.Height*scale);
            bool pressed=pitch==key_preview_pitch;
            Gdiplus::GraphicsPath key_path;
            add_rounded_rectangle(key_path,key,black ? 2.0f*scale : 0.5f*scale);
            Gdiplus::SolidBrush fill(pressed
                ? (black ? Gdiplus::Color(255,40,125,169) : Gdiplus::Color(255,134,223,244))
                : (black ? Gdiplus::Color(255,36,40,47) : Gdiplus::Color(255,226,230,236)));
            graphics.FillPath(&fill,&key_path);
            Gdiplus::Pen edge(pressed ? Gdiplus::Color(255,154,241,255)
                : Gdiplus::Color(255,133,144,157),pressed ? 2.0f*scale : 0.7f*scale);
            graphics.DrawPath(&edge,&key_path);
            if (pitch<roll_low_pitch || pitch>roll_high_pitch) { continue; }
            std::wostringstream name;
            name<<pitch_names[static_cast<std::size_t>(pitch%12)]<<pitch/12-1;
            float label_y=grid_top+(roll_high_pitch-pitch)*row_height;
            draw_centered_text(graphics,name.str(),
                Gdiplus::RectF(black ? left+scale : left+52.0f*scale,label_y,
                    (black ? 51.0f : 30.0f)*scale,row_height),
                std::min(11.0f*scale,row_height*0.7f),true,
                black ? Gdiplus::Color(255,255,255,255) : Gdiplus::Color(255,35,42,52));
        }
    }
    Gdiplus::Pen note_edge(Gdiplus::Color(255,166,244,255),1.5f*scale);
    for (std::size_t position=0;position<chord_blocks.size();++position) {
        const auto& chord=displayed_progression.chords[position];
        std::wstring caption=chord.notes.empty()
            ? L"双击输入和弦内音"
            : utf8_to_wide(chord.notes);
        float x=offset_x+static_cast<float>(timeline_x(block_start(position)))*scale;
        float width=static_cast<float>(timeline_x(block_end(position))-timeline_x(block_start(position)))*scale;
        Gdiplus::SolidBrush block_header(position==static_cast<std::size_t>(active_slot)
            ? Gdiplus::Color(180,40,78,103) : Gdiplus::Color(160,38,47,66));
        graphics.FillRectangle(&block_header,x+1.0f,top+1.0f,width-2.0f,header_height-2.0f);
        draw_centered_text(
            graphics,caption,
            Gdiplus::RectF(
                x,top,width,header_height
            ),
            std::max(8.0f*scale,std::min(16.0f*scale,width/8.0f)),
            !chord.notes.empty(),
            chord.notes.empty() ? Gdiplus::Color(180,205,214,230)
                                : Gdiplus::Color(255,245,248,255)
        );

    }
    for (std::size_t event_index=0;event_index<rhythm.events.size();++event_index) {
        const auto& event=rhythm.events[event_index];
        std::size_t block=event_block(event_index);
        for (int pitch:event.pitches) {
            int lane=roll_high_pitch-pitch;
            int pitch_class=pitch%12;
            float note_x=offset_x+static_cast<float>(timeline_x(event.start)+1.0)*scale;
            float padding=std::min(2.0f*scale,row_height*0.15f);
            float note_y=grid_top+lane*row_height+padding;
            float note_width=std::max(2.0f*scale,static_cast<float>(818.0*event.duration/rhythm.length-2.0)*scale);
            float note_height=std::max(1.0f,row_height-2.0f*padding);
            Gdiplus::RectF note_rect(
                note_x,note_y,note_width,note_height
            );
            Gdiplus::GraphicsPath note_path;
            add_rounded_rectangle(note_path,note_rect,5.0f*scale);
            Gdiplus::LinearGradientBrush note_fill(
                Gdiplus::PointF(note_x,note_y),Gdiplus::PointF(note_x,note_y+note_height),
                block%2 ? Gdiplus::Color(255,127,214,239) : Gdiplus::Color(255,78,218,221),
                block%2 ? Gdiplus::Color(255,65,147,210) : Gdiplus::Color(255,34,161,184)
            );
            graphics.FillPath(&note_fill,&note_path);
            graphics.DrawPath(&note_edge,&note_path);
            if (note_width>25.0f*scale && row_height>10.0f*scale) { draw_centered_text(
                graphics,pitch_names[static_cast<std::size_t>(pitch_class)],
                note_rect,12.0f*scale,true,Gdiplus::Color(255,12,52,66)
            ); }
        }
    }

    graphics.Restore(state);
    Gdiplus::Pen outline(Gdiplus::Color(255,165,180,205),2.0f*scale);
    graphics.DrawPath(&outline,&rounded);

    // 中键可切换任意相邻节奏事件间的分块，未分块处使用低亮度刻度。
    for (std::size_t i=1;i<rhythm.events.size();++i) {
        float boundary=static_cast<float>(timeline_x(event_boundary(i)));
        if (!rhythm_splits[i-1]) {
            Gdiplus::Pen tick(Gdiplus::Color(110,176,207,226),1.0f*scale);
            float x=offset_x+boundary*scale;
            graphics.DrawLine(&tick,x,offset_y+655.0f*scale,x,offset_y+666.0f*scale);
            continue;
        }
        auto arrow=arrow_bounds(i);
        float size=arrow.Width*scale;
        float x=offset_x+arrow.X*scale;
        float y=offset_y+arrow.Y*scale;
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

    for (std::size_t block=0;block<chord_blocks.size();++block) {
        auto logical=emotion_bounds(block);
        Gdiplus::RectF bounds(offset_x+logical.X*scale,offset_y+logical.Y*scale,
            logical.Width*scale,logical.Height*scale);
        Gdiplus::GraphicsPath path;
        add_rounded_rectangle(path,bounds,std::min(16.0f,logical.Width*0.15f)*scale);
        auto saved=graphics.Save();
        graphics.SetClip(&path);
        Gdiplus::SolidBrush fill(Gdiplus::Color(235,42,61,86));
        graphics.FillPath(&fill,&path);
        int preset=slot_presets[block];
        if (preset>=0 && emotion_images[static_cast<std::size_t>(preset)]) {
            graphics.DrawImage(emotion_images[static_cast<std::size_t>(preset)].get(),bounds);
        }
        else { draw_centered_text(graphics,L"＋",bounds,std::min(28.0f,logical.Width*0.4f)*scale); }
        graphics.Restore(saved);
        Gdiplus::Pen edge(block==static_cast<std::size_t>(active_slot)
            ? Gdiplus::Color(255,139,234,247) : Gdiplus::Color(150,122,155,192),
            block==static_cast<std::size_t>(active_slot) ? 2.5f*scale : scale);
        graphics.DrawPath(&edge,&path);
        std::wostringstream label;
        label<<block+1<<L" · "<<utf8_to_wide(ncnl::emotion_preset_label(preset));
        float left=static_cast<float>(timeline_x(block_start(block)));
        float cell=static_cast<float>(timeline_x(block_end(block)))-left;
        draw_centered_text(graphics,label.str(),
            Gdiplus::RectF(offset_x+left*scale,
                offset_y+(SLOT_Y+logical.Height+12.0f)*scale,cell*scale,24.0f*scale),
            std::min(14.0f,cell*0.13f)*scale,false);
    }
    std::wostringstream info;
    info<<(rhythm_file_name.empty() ? L"默认节奏" : rhythm_file_name)
        <<L"  ·  "<<rhythm.events.size()<<L" 个节奏音符 / "<<chord_blocks.size()<<L" 个分块";
    draw_centered_text(graphics,info.str(),
        Gdiplus::RectF(offset_x+50.0f*scale,offset_y+253.0f*scale,900.0f*scale,30.0f*scale),
        14.0f*scale,false,Gdiplus::Color(245,207,227,242));
    draw_centered_text(graphics,L"拖入 MIDI 导入节奏 · 中键点击音符间隙分块/合并 · 空格播放",
        Gdiplus::RectF(offset_x+70.0f*scale,offset_y+905.0f*scale,860.0f*scale,28.0f*scale),
        13.0f*scale,false,Gdiplus::Color(225,214,228,242));

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
    if (animation_dc) {
        SelectObject(animation_dc,animation_old_bitmap);
        DeleteObject(animation_bitmap);
        DeleteDC(animation_dc);
        animation_dc=nullptr;
        animation_bitmap=nullptr;
    }
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

float particle_random(unsigned seed) {
    seed^=seed>>16; seed*=0x7feb352du; seed^=seed>>15;
    seed*=0x846ca68bu; seed^=seed>>16;
    return (seed&0xffffu)/65535.0f;
}

Gdiplus::PointF radial_particle_position(const Gdiplus::RectF& bounds,float angle,float distance) {
    return Gdiplus::PointF(bounds.X+bounds.Width*0.5f+std::cos(angle)*distance,
        bounds.Y+bounds.Height*0.5f+std::sin(angle)*distance);
}

Gdiplus::PointF playback_particle_position(const Gdiplus::RectF& bounds,
    unsigned seed,unsigned spark,unsigned count,float angle,float distance) {
    // Spread emission sites across the whole note, then radiate from each site.
    float u=(spark+particle_random(seed+spark*67))/count;
    float v=particle_random(seed+spark*79);
    return Gdiplus::PointF(bounds.X+bounds.Width*u+std::cos(angle)*distance,
        bounds.Y+bounds.Height*v+std::sin(angle)*distance);
}

void draw_light_particle(Gdiplus::Graphics& graphics,float x,float y,
    float radius,float opacity,bool violet,bool star) {
    if (x<122 || x>960 || y<337 || y>655) { return; }
    BYTE alpha=static_cast<BYTE>(std::max(0.0f,std::min(255.0f,opacity)));
    BYTE red=violet?205:125,green=violet?182:232;
    for (int layer=3;layer>=2;--layer) {
        float r=radius*layer;
        Gdiplus::SolidBrush halo(Gdiplus::Color(alpha/(layer*3),red,green,255));
        graphics.FillEllipse(&halo,x-r,y-r,r*2,r*2);
    }
    Gdiplus::SolidBrush core(Gdiplus::Color(alpha,221,247,255));
    graphics.FillEllipse(&core,x-radius,y-radius,radius*2,radius*2);
    if (star) {
        Gdiplus::Pen ray(Gdiplus::Color(alpha/2,red,green,255),0.7f);
        graphics.DrawLine(&ray,x-radius*3,y,x+radius*3,y);
        graphics.DrawLine(&ray,x,y-radius*3,x,y+radius*3);
    }
}

void draw_dissolve_overlay(HDC dc) {
    if (dissolving_notes.empty()) { return; }
    Gdiplus::Graphics graphics(dc);
    graphics.ScaleTransform(static_cast<float>(current_scale),static_cast<float>(current_scale));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath clip;
    add_rounded_rectangle(clip,Gdiplus::RectF(50,295,900,350),18);
    graphics.SetClip(&clip);
    graphics.SetClip(Gdiplus::RectF(132,347,818,298),Gdiplus::CombineModeIntersect);
    ULONGLONG now=monotonic_ms();
    for (const auto& note:dissolving_notes) {
        float age=static_cast<float>(now-note.born)/DISSOLVE_DURATION_MS;
        if (age>=1.0f) { continue; }
        const auto& box=note.bounds;
        float fade=(1.0f-age)*(1.0f-age);
        float drift=1.0f-std::pow(1.0f-age,3.0f);
        // The original silhouette rapidly dissolves; fragments inherit its exact rectangle.
        if (age<0.22f) {
            Gdiplus::GraphicsPath ghost;
            add_rounded_rectangle(ghost,box,5);
            Gdiplus::SolidBrush fill(Gdiplus::Color(
                static_cast<BYTE>(155*(1.0f-age/0.22f)),92,207,231));
            graphics.FillPath(&fill,&ghost);
        }
        unsigned count=static_cast<unsigned>(std::max(std::size_t(8),
            std::min(std::size_t(32),std::size_t(512)/dissolving_notes.size())));
        for (unsigned i=0;i<count;++i) {
            float u=particle_random(note.seed+i*19);
            float v=particle_random(note.seed+i*31);
            float angle=(i+particle_random(note.seed+i*43))*6.2831853f/count;
            float speed=18.0f+particle_random(note.seed+i*59)*40.0f;
            float vx=std::cos(angle)*speed;
            float vy=std::sin(angle)*speed;
            auto origin=radial_particle_position(box,angle,speed*drift);
            float x=origin.X+(u-0.5f)*box.Width*drift;
            float y=origin.Y+(v-0.5f)*box.Height*drift;
            float radius=(0.8f+particle_random(note.seed+i*71)*1.4f)*(1.0f-0.6f*age);
            Gdiplus::Pen trail(Gdiplus::Color(static_cast<BYTE>(75*fade),154,225,255),0.6f);
            graphics.DrawLine(&trail,x-vx*age*0.13f,y-vy*age*0.13f,x,y);
            draw_light_particle(graphics,x,y,radius,215*fade,note.alternate,i%7==0);
        }
    }
}

void draw_playback_overlay(HDC dc) {
    if (playback_loop_ms<=0.0) { return; }
    double elapsed=static_cast<double>(monotonic_ms()-playback_start_ms);
    double beat=std::fmod(elapsed,playback_loop_ms)/playback_loop_ms*rhythm.length;
    float scale=static_cast<float>(current_scale);
    Gdiplus::Graphics graphics(dc);
    graphics.ScaleTransform(scale,scale);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath roll_clip;
    add_rounded_rectangle(roll_clip,Gdiplus::RectF(50,295,900,350),18);
    graphics.SetClip(&roll_clip);
    graphics.SetClip(Gdiplus::RectF(132,347,818,298),Gdiplus::CombineModeIntersect);
    float x=static_cast<float>(timeline_x(beat));
    for (int glow=3;glow>=1;--glow) {
        Gdiplus::Pen pen(Gdiplus::Color(glow==1?235:22,153,228,255),
            glow==1?1.5f:glow*5.0f);
        graphics.DrawLine(&pen,x,347.0f,x,645.0f);
    }
    int particles=0;
    for (std::size_t i=0;i<rhythm.events.size();++i) {
        const auto& event=rhythm.events[i];
        if (beat<event.start || beat>=event.start+event.duration) { continue; }
        for (int pitch:event.pitches) {
            auto bounds=visual_note_bounds(event,pitch);
            float pulse=static_cast<float>(0.5+0.5*std::sin(elapsed/180.0));
            Gdiplus::GraphicsPath path;
            add_rounded_rectangle(path,bounds,5);
            Gdiplus::SolidBrush lit(Gdiplus::Color(static_cast<BYTE>(35+25*pulse),189,248,255));
            Gdiplus::Pen rim(Gdiplus::Color(static_cast<BYTE>(150+70*pulse),205,247,255),1.8f);
            graphics.FillPath(&lit,&path); graphics.DrawPath(&rim,&path);
            bool violet=event_block(i)%2!=0;
            unsigned seed=static_cast<unsigned>(i*137+pitch*73);
            double note_elapsed=(beat-event.start)*playback_loop_ms/rhythm.length;
            for (unsigned spark=0;spark<18 && particles<384;++spark,++particles) {
                float phase=static_cast<float>(std::fmod(note_elapsed/800.0+
                    particle_random(seed+spark*97),1.0));
                float angle=(spark+particle_random(seed+spark*31))*6.2831853f/18.0f;
                float distance=phase*(18+particle_random(seed+spark*43)*30);
                auto position=playback_particle_position(bounds,seed,spark,18,angle,distance);
                float fade=std::sin(phase*3.14159265f);
                float radius=0.7f+particle_random(seed+spark*59)*1.1f;
                draw_light_particle(graphics,position.X,position.Y,radius,185*fade,violet,spark%6==0);
            }
            float y=bounds.Y+bounds.Height*0.5f;
            draw_light_particle(graphics,x,y,2.1f,190,violet,true);
        }
    }
}

void paint_window(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc=BeginPaint(window,&paint);
    RECT client{};
    GetClientRect(window,&client);
    int width=client.right-client.left;
    int height=client.bottom-client.top;
    if (width>0 && height>0 && ensure_main_background_buffer(window,dc)) {
        HDC source=main_background_dc;
        if ((playback_loop_ms>0.0 || !dissolving_notes.empty()) && !interactive_resize) {
            if (!animation_dc) {
                animation_dc=CreateCompatibleDC(dc);
                animation_bitmap=CreateCompatibleBitmap(dc,width,height);
                if (animation_dc && animation_bitmap) {
                    animation_old_bitmap=SelectObject(animation_dc,animation_bitmap);
                } else {
                    if (animation_dc) { DeleteDC(animation_dc); }
                    if (animation_bitmap) { DeleteObject(animation_bitmap); }
                    animation_dc=nullptr; animation_bitmap=nullptr;
                }
            }
            if (animation_dc) {
                const RECT& dirty=paint.rcPaint;
                BitBlt(animation_dc,dirty.left,dirty.top,dirty.right-dirty.left,dirty.bottom-dirty.top,
                    main_background_dc,dirty.left,dirty.top,SRCCOPY);
                draw_playback_overlay(animation_dc);
                draw_dissolve_overlay(animation_dc);
                source=animation_dc;
            }
        }
        const RECT& dirty=paint.rcPaint;
        BitBlt(dc,dirty.left,dirty.top,dirty.right-dirty.left,dirty.bottom-dirty.top,
            source,dirty.left,dirty.top,SRCCOPY);
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
    // 取外层边线与径向粗线的交点，避开向外延伸的笔锋。
    // 原图约 (720,176)、(1329,582)，映射到 (70,20,660,660)。
    Gdiplus::PointF(398.0f,100.0f),
    Gdiplus::PointF(675.0f,285.0f),
    Gdiplus::PointF(587.0f,639.0f),
    // 按 idea/map.png 的红线校准：原图约 (352,1300)/1450，
    // 映射到图片区域 (70,20,660,660) 后为 (230,612)。
    // 此端点由轴线绘制、权重点插值和鼠标拖动投影共用。
    Gdiplus::PointF(230.0f,612.0f),
    // 声部运动：原图左侧交点约 (112,582)。
    Gdiplus::PointF(121.0f,285.0f),
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
        initialize_rhythm();
        DragAcceptFiles(window,TRUE);
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

        create_control(L"BUTTON",L"导入 MIDI",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            475,32,95,52,window,ID_IMPORT_MIDI,FontKind::hint);
        create_control(L"BUTTON",L"导出 MIDI",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            580,32,95,52,window,ID_EXPORT_MIDI,FontKind::hint);

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
        int slot=slot_at_client_point(window,point);
        if (slot>=0) {
            active_slot=slot;
            update_slot_ui();
            return 0;
        }
        int key_pitch=keyboard_pitch_at_client_point(window,point);
        if (key_pitch>=0) {
            SetFocus(window);
            if (play_key_preview(window,key_pitch)) {
                key_preview_gesture=true;
                SetCapture(window);
            }
            return 0;
        }
        if (begin_midi_note_drag(window,point)) {
            return 0;
        }
        if (create_midi_note_at_client_point(window,point)) { return 0; }
        break;
    }

    case WM_MOUSEMOVE:
        if (key_preview_gesture) {
            POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
            int pitch=keyboard_pitch_at_client_point(window,point);
            if (pitch<0) { stop_key_preview(); }
            else if (pitch!=key_preview_pitch) { play_key_preview(window,pitch); }
            return 0;
        }
        if (dragged_midi_position>=0) {
            POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
            update_midi_note_drag(window,point);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (key_preview_gesture) {
            key_preview_gesture=false;
            release_key_preview();
            if (GetCapture()==window) { ReleaseCapture(); }
            return 0;
        }
        if (dragged_midi_position>=0) {
            POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
            end_midi_note_drag(window,point);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (key_preview_gesture) { stop_key_preview(); }
        key_preview_gesture=false;
        if (dragged_midi_position>=0) {
            dragged_midi_position=-1;
            dragged_midi_event=-1;
            dragged_midi_pitch=-1;
        }
        return 0;

    case WM_CLEAR_CHORD_HOVER: {
        key_preview_gesture=false;
        stop_key_preview();
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        clear_chord_at_client_point(window,point);
        return 0;
    }

    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        key_preview_gesture=false;
        stop_key_preview();
        if (GetCapture()==window) { ReleaseCapture(); }
        break;

    case WM_ACTIVATEAPP:
        if (!w_param) {
            key_preview_gesture=false;
            stop_key_preview();
        }
        break;

    case WM_MBUTTONDOWN:
        toggle_rhythm_split(window,POINT{GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)});
        return 0;

    case WM_DROPFILES: {
        HDROP drop=reinterpret_cast<HDROP>(w_param);
        POINT point{};
        DragQueryPoint(drop,&point);
        UINT length=DragQueryFileW(drop,0,nullptr,0);
        std::vector<wchar_t> path(length+1);
        DragQueryFileW(drop,0,path.data(),length+1);
        DragFinish(drop);
        RECT client{}; GetClientRect(window,&client);
        double scale=std::min(client.right/1000.0,client.bottom/1000.0);
        if (scale>0 && point.x/scale>=50 && point.x/scale<=950 &&
            point.y/scale>=295 && point.y/scale<=645 && length>0) {
            import_midi_file(window,path.data());
        }
        return 0;
    }

    case WM_TIMER:
        if (w_param==KEY_PREVIEW_TIMER) {
            if (!key_preview_gesture) { stop_key_preview(); }
            return 0;
        }
        if (w_param==DISSOLVE_TIMER) { expire_dissolving_notes(monotonic_ms()); }
        if ((w_param==PLAYBACK_TIMER || w_param==DISSOLVE_TIMER) && !IsIconic(window)) {
            RECT area={static_cast<LONG>(132*current_scale),static_cast<LONG>(347*current_scale),
                static_cast<LONG>(951*current_scale),static_cast<LONG>(646*current_scale)};
            InvalidateRect(window,&area,FALSE);
        }
        return 0;

    case WM_ANIMATION_FRAME: {
        InterlockedExchange(&animation_frame_pending,0);
        expire_dissolving_notes(monotonic_ms());
        if (!IsIconic(window) && !interactive_resize) {
            RECT area={static_cast<LONG>(132*current_scale),static_cast<LONG>(347*current_scale),
                static_cast<LONG>(951*current_scale),static_cast<LONG>(646*current_scale)};
            InvalidateRect(window,&area,FALSE);
            UpdateWindow(window);
        }
        return 0;
    }

    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;

    case WM_ENTERSIZEMOVE:
        commit_chord_editor(false);
        interactive_resize=true;
        main_background_dirty=true;
        update_animation_clock();
        return 0;

    case WM_SIZE:
        expire_dissolving_notes(monotonic_ms());
        update_animation_clock();
        if (w_param!=SIZE_MINIMIZED && !interactive_resize && !controls.empty()) {
            apply_layout(window);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        interactive_resize=false;
        main_background_dirty=true;
        update_animation_clock();
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
        // 鼠标右键清除已在按下/移动时执行；松开时不再重复清除。
        if (l_param!=-1) {
            return 0;
        }
        slot_presets[active_slot]=-1;
        update_slot_ui();
        return 0;
    }

    case WM_COMMAND: {
        int id=LOWORD(w_param);
        int notification=HIWORD(w_param);
        if (editing_chord>=0 &&
            reinterpret_cast<HWND>(l_param)!=chord_editor &&
            !commit_chord_editor(true)) {
            return 0;
        }
        if ((id==ID_IMPORT_MIDI || id==ID_EXPORT_MIDI) && notification==BN_CLICKED) {
            choose_midi_file(window,id==ID_EXPORT_MIDI);
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
        stop_animation_clock();
        KillTimer(window,DISSOLVE_TIMER);
        dissolving_notes.clear();
        stop_midi_playback();
        stop_key_preview();
        if (shared_midi_output && !midi_playback_thread) {
            midi_output_api.reset(shared_midi_output);
            midi_output_api.close(shared_midi_output);
            shared_midi_output=nullptr;
        }
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

    load_interface_font(executable_directory()+L"\\assets\\res\\font.ttf");

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
        unload_interface_font();
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
        unload_interface_font();
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
        unload_interface_font();
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
            (message.message==WM_RBUTTONDOWN ||
             (message.message==WM_MOUSEMOVE && (message.wParam&MK_RBUTTON)))) {
            POINT point={GET_X_LPARAM(message.lParam),GET_Y_LPARAM(message.lParam)};
            MapWindowPoints(message.hwnd,main_window,&point,1);
            if (message.message==WM_RBUTTONDOWN) {
                dragged_midi_position=-1;
                dragged_midi_event=-1;
                dragged_midi_pitch=-1;
                SetCapture(main_window);
            }
            SendMessageW(
                main_window,WM_CLEAR_CHORD_HOVER,0,
                MAKELPARAM(point.x,point.y)
            );
            continue;
        }
        if (main_window_key && message.message==WM_RBUTTONUP) {
            if (GetCapture()==main_window) {
                ReleaseCapture();
            }
            continue;
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
    unload_interface_font();
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return static_cast<int>(message.wParam);
}
