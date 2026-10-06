#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <set>
#include "../algorithms/chord_algorithms.hpp"
#include "../generation/chord_generator.hpp"
#include "../config/progression_config.hpp"
#include "../midi/piano_roll_notes.hpp"
#include "../midi/midi_rhythm.hpp"
#include "../midi/midi_playback.hpp"
#include "mouse_feedback.hpp"
#include "preset_library.hpp"
#include "midi_file_drag.hpp"
#include "pixel_skin.hpp"
#include "button_artwork.hpp"
#include "window_layout.hpp"
#include "../plugins/plugin_manager.hpp"

int BPM=120;

namespace {

constexpr int ID_GENERATE=1003;
constexpr int ID_SETTINGS=1004;
constexpr int ID_PRESET_ACTION=1005;
constexpr UINT_PTR PLAYBACK_TIMER=2;
constexpr UINT_PTR DISSOLVE_TIMER=3;
constexpr UINT_PTR KEY_PREVIEW_TIMER=4;
constexpr UINT_PTR EMOTION_GIF_TIMER=5;
constexpr UINT_PTR HEADER_DRAG_TIMER=6;
constexpr ULONGLONG HEADER_DRAG_HOLD_MS=350;
constexpr std::size_t UNDO_HISTORY_LIMIT=20;
constexpr int ID_PRESET_BASE=1100;
constexpr UINT WM_CLEAR_CHORD_HOVER=WM_APP+1;
constexpr UINT WM_ANIMATION_FRAME=WM_APP+2;
constexpr double TARGET_ANIMATION_FPS=120.0;

constexpr int DESIGN_WIDTH=1000;
constexpr int DESIGN_HEIGHT=1000;
constexpr int MIN_CLIENT_SIZE=600;
constexpr int SETTINGS_DESIGN_SIZE=800;
constexpr int SLOT_Y=735;
constexpr float ROLL_TOP=255.0f;
constexpr float ROLL_GRID_TOP=ROLL_TOP+52.0f;
constexpr float ROLL_GRID_HEIGHT=645.0f-ROLL_GRID_TOP;

enum class FontKind {
    title,
    normal,
    card,
};

struct ControlLayout {
    HWND window;
    int x;
    int y;
    int width;
    int height;
    FontKind font_kind;
};

std::vector<ControlLayout> controls;

HWND main_window=nullptr;
int header_drag_block=-1;
POINT header_drag_press={0,0},header_drag_pointer={0,0};
ULONGLONG header_drag_started=0;
ncnl::MouseFeedback mouse_feedback;
ncnl::MouseFeedback configuration_mouse_feedback;
bool middle_dragging=false;
POINT middle_previous={0,0};
std::set<std::size_t> middle_visited;
HWND configuration_window=nullptr;
HWND settings_button=nullptr;
HWND mode_label=nullptr;
HWND generate_button=nullptr;
HWND chord_editor=nullptr;
WNDPROC chord_editor_procedure_original=nullptr;
std::array<HWND,5> preset_buttons={{nullptr,nullptr,nullptr,nullptr,nullptr}};
std::vector<int> slot_presets(4,-1);
std::array<WNDPROC,5> preset_button_procedures={{nullptr,nullptr,nullptr,nullptr,nullptr}};
int active_slot=0;
int dragged_preset=-1;
int drag_hover_slot=-1;
bool preset_dragging=false;
POINT preset_drag_start={0,0};

HFONT title_font=nullptr;
HFONT normal_font=nullptr;
HFONT card_font=nullptr;
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
HANDLE registered_font=nullptr;
ncnl::PixelSkin background_image;
ncnl::ButtonArtworks button_artworks;
ncnl::ButtonArtwork pentagon_image;
ncnl::PluginManager plugin_manager;
ncnl::ButtonArtwork ncnl_button_art,plugin_tab_art;
std::vector<std::unique_ptr<ncnl::ButtonArtwork>> plugin_button_art;
HWND plugin_container=nullptr;
int plugin_scroll=0;
void resize_plugin_page(HWND window);
void update_plugin_interface(HWND window);
std::array<ncnl::AssetImage,5> emotion_images;
struct EmotionAnimation {
    std::vector<ULONGLONG> delays;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> frames;
    UINT frame=0;
    ULONGLONG next_frame=0;
    ULONGLONG cycle=0;
};
std::array<EmotionAnimation,5> emotion_animations;
ncnl::AssetImage arrow_image;
std::string progression_config_path;
bool configuration_interactive_resize=false;
constexpr int CONFIG_TAB_MODE=0,CONFIG_TAB_RADAR=1,CONFIG_TAB_PLUGINS=2;
std::array<ncnl::PixelSkin,3> configuration_skins;
int configuration_tab=CONFIG_TAB_MODE;
constexpr UINT_PTR CONFIGURATION_TRANSITION_TIMER=73;
ULONGLONG configuration_opened=0,configuration_switched=0,wheel_started=0;
bool configuration_closing=false,wheel_dragging=false,wheel_moved=false;
double wheel_rotation=0,wheel_from=0,wheel_target=0,wheel_press_angle=0;
int pending_tonic=0,pending_mode=0;
const std::array<int,12> FIFTHS={{0,7,2,9,4,11,6,1,8,3,10,5}};
const std::array<const char*,12> TONIC_NAMES={{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
const std::array<const char*,7> MODE_NAMES={{"Ionian","Dorian","Phrygian","Lydian","Mixolydian","Aeolian","Locrian"}};
HDC configuration_dc=nullptr;
HBITMAP configuration_bitmap=nullptr;
HGDIOBJ configuration_old_bitmap=nullptr;
int configuration_width=0,configuration_height=0;
bool configuration_animating=false;
int configuration_opacity=-1;
HDC configuration_static_dc=nullptr;
HBITMAP configuration_static_bitmap=nullptr;
HGDIOBJ configuration_static_old=nullptr;
int configuration_cached_tab=-1,configuration_cached_tonic=-1,configuration_cached_mode=-1;
std::array<double,5> configuration_cached_weights={{-1,-1,-1,-1,-1}};
std::array<std::unique_ptr<Gdiplus::Bitmap>,36> wheel_glyphs;
float wheel_glyph_scale=0;
constexpr float WHEEL_X=400,WHEEL_Y=285,WHEEL_RADIUS=265,WHEEL_INNER=158,WHEEL_LABEL_RADIUS=212;
int dragged_weight_axis=-1;
std::string current_mode="C Ionian";
ncnl::GeneratedProgression displayed_progression{
    std::vector<ncnl::GeneratedChord>(4),0.0
};
ncnl::MidiRhythm rhythm{
};
std::vector<bool> rhythm_splits(3,true);
struct ChordBlock { std::size_t first,last; };
std::vector<ChordBlock> chord_blocks;
int dragged_midi_event=-1;
int roll_low_pitch=60;
int roll_high_pitch=71;
ULONGLONG playback_start_ms=0;
double playback_loop_ms=0.0;
struct DissolvingNote {
    Gdiplus::RectF bounds;
    ULONGLONG born;
    unsigned seed;
    bool alternate;
    bool arrow;
};
std::vector<DissolvingNote> dissolving_notes;
constexpr ULONGLONG DISSOLVE_DURATION_MS=760;
std::wstring rhythm_file_name;
struct EditSnapshot {
    ncnl::MidiRhythm rhythm;
    std::vector<bool> splits;
    std::vector<ChordBlock> blocks;
    ncnl::GeneratedProgression progression;
    std::vector<int> presets;
    std::wstring file_name;
    int active,low,high;
};
std::vector<EditSnapshot> undo_history;
bool midi_drag_recorded=false;

void remember_edit() {
    if (undo_history.size()>=UNDO_HISTORY_LIMIT) { undo_history.erase(undo_history.begin()); }
    undo_history.push_back({rhythm,rhythm_splits,chord_blocks,displayed_progression,
        slot_presets,rhythm_file_name,active_slot,roll_low_pitch,roll_high_pitch});
}
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
    float row=ROLL_GRID_HEIGHT/(roll_high_pitch-roll_low_pitch+1);
    float padding=std::min(2.0f,row*0.15f);
    return Gdiplus::RectF(static_cast<float>(timeline_x(event.start)+1.0),
        ROLL_GRID_TOP+(roll_high_pitch-pitch)*row+padding,
        std::max(2.0f,static_cast<float>(818.0*event.duration/rhythm.length-2.0)),
        std::max(1.0f,row-2.0f*padding));
}

void start_note_dissolve(std::size_t event,int pitch) {

    if (dissolving_notes.size()>=64) { dissolving_notes.erase(dissolving_notes.begin()); }
    auto born=monotonic_ms();
    dissolving_notes.push_back({visual_note_bounds(rhythm.events[event],pitch),born,
        static_cast<unsigned>(event*137+pitch*73+born),event_block(event)%2!=0,false});
    update_animation_clock();
}

void start_arrow_dissolve(const Gdiplus::RectF& bounds) {
    if (dissolving_notes.size()>=64) { dissolving_notes.erase(dissolving_notes.begin()); }
    auto born=monotonic_ms();
    dissolving_notes.push_back({bounds,born,static_cast<unsigned>(born+bounds.X*137),true,true});
    update_animation_clock();
}

RECT animation_effect_bounds() {
    bool arrow=std::any_of(dissolving_notes.begin(),dissolving_notes.end(),
        [](const DissolvingNote& note){ return note.arrow; });
    return {static_cast<LONG>(132*current_scale),static_cast<LONG>(ROLL_GRID_TOP*current_scale),
        static_cast<LONG>(951*current_scale),static_cast<LONG>((arrow ? 735 : 646)*current_scale)};
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
    KillTimer(main_window,PLAYBACK_TIMER);
    if (animation_period_active) { timeEndPeriod(1); animation_period_active=false; }

    if (main_window) {
        MSG stale{};
        while (PeekMessageW(&stale,main_window,WM_ANIMATION_FRAME,WM_ANIMATION_FRAME,PM_REMOVE)) {}
    }
    InterlockedExchange(&animation_frame_pending,0);
}

void update_animation_clock() {
    bool needed=main_window && IsWindow(main_window) && !IsIconic(main_window) &&
        !interactive_resize && (playback_loop_ms>0 || !dissolving_notes.empty() ||
        mouse_feedback.active() || configuration_mouse_feedback.active() || ncnl::preset_library_feedback().active() ||
        (configuration_animating && configuration_window && !IsIconic(configuration_window) && !configuration_interactive_resize));
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



void initialize_runtime_storage(const std::wstring& directory) {
    for (const auto& name:{L"presents",L"plugins"}) {
        auto path=directory+L"\\"+name;
        DWORD attributes=GetFileAttributesW(path.c_str());
        if (attributes==INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryW(path.c_str(),nullptr)) { throw std::runtime_error("无法创建运行数据目录，请检查程序所在目录的写入权限。"); }
        } else if (!(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
            throw std::runtime_error("presents 或 plugins 已被同名文件占用，请先移走该文件。");
        }
    }
    auto path=directory+L"\\config.json"; progression_config_path=wide_to_utf8(path);
    DWORD attributes=GetFileAttributesW(path.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) {
        if (GetLastError()!=ERROR_FILE_NOT_FOUND) { throw std::runtime_error("无法访问 config.json，请检查目录权限。"); }
        ncnl::set_progression_weights(ncnl::default_progression_weights());
        if (!ncnl::save_progression_config(progression_config_path)) { throw std::runtime_error("无法创建默认 config.json。"); }
    } else if (!ncnl::load_progression_config(progression_config_path)) {
        ncnl::set_progression_weights(ncnl::default_progression_weights());
        MessageBoxW(nullptr,L"config.json 无法读取或内容无效，本次使用默认参数，原文件未改动。",L"配置提示",MB_OK|MB_ICONWARNING);
    }
}

void load_application_skins(const std::wstring& assets) {
    const std::wstring directory=assets+L"\\skins\\";
    background_image.reset(); background_image.load(directory+L"main.png"); main_background_dirty=true;
    configuration_skins[CONFIG_TAB_MODE].reset(); configuration_skins[CONFIG_TAB_RADAR].reset();
    configuration_skins[CONFIG_TAB_MODE].load(directory+L"mode.png");
    configuration_skins[CONFIG_TAB_RADAR].load(directory+L"radar.png");
    configuration_skins[CONFIG_TAB_PLUGINS].load(directory+L"plugins.png");
    ncnl_button_art.load(directory+L"buttons\\ncnl.png",true);
    plugin_tab_art.load(directory+L"buttons\\plugins_tab.png",true);
    pentagon_image.load(assets+L"\\res\\penta_dim.png");
    button_artworks.load(directory+L"buttons");
    configuration_cached_tab=-1;
}

ncnl::AssetImage load_png(const std::wstring& path) {
    auto image=ncnl::asset_image(path);
    if (!image || image->GetLastStatus()!=Gdiplus::Ok ||
        image->GetWidth()==0 || image->GetHeight()==0) {
        return {};
    }
    return image;
}

ncnl::AssetImage load_emotion_icon(const std::wstring& base) {
    auto image=load_png(base+L".gif");
    if (!image) { image=load_png(base+L".png"); }
    return image;
}

void initialize_emotion_animation(std::size_t index,ULONGLONG now) {
    auto& animation=emotion_animations[index];
    animation=EmotionAnimation{};
    auto image=emotion_images[index].get();
    if (!image) { return; }
    UINT count=image->GetFrameCount(&Gdiplus::FrameDimensionTime);
    if (count<2 || image->GetLastStatus()!=Gdiplus::Ok) { return; }
    animation.delays.assign(count,100);
    UINT bytes=image->GetPropertyItemSize(PropertyTagFrameDelay);
    if (bytes>=sizeof(Gdiplus::PropertyItem)) {
        std::vector<BYTE> storage(bytes);
        auto property=reinterpret_cast<Gdiplus::PropertyItem*>(storage.data());
        if (image->GetPropertyItem(PropertyTagFrameDelay,bytes,property)==Gdiplus::Ok &&
            property->type==PropertyTagTypeLong && property->value && property->length/sizeof(ULONG)>=count) {
            auto delays=static_cast<const ULONG*>(property->value);
            for (UINT frame=0;frame<count;++frame) {

                animation.delays[frame]=std::max<ULONGLONG>(10,static_cast<ULONGLONG>(delays[frame])*10);
            }
        }
    }
    for (auto delay:animation.delays) { animation.cycle+=delay; }

    float side=static_cast<float>(std::min(image->GetWidth(),image->GetHeight()));
    for (UINT frame=0;frame<count;++frame) {
        if (image->SelectActiveFrame(&Gdiplus::FrameDimensionTime,frame)!=Gdiplus::Ok) {
            animation.frames.clear(); break;
        }
        std::unique_ptr<Gdiplus::Bitmap> bitmap(new Gdiplus::Bitmap(160,160,PixelFormat32bppPARGB));
        Gdiplus::Graphics graphics(bitmap.get());
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(image,Gdiplus::RectF(0,0,160,160),
            (image->GetWidth()-side)*0.5f,(image->GetHeight()-side)*0.5f,side,side,Gdiplus::UnitPixel);
        animation.frames.push_back(std::move(bitmap));
    }
    animation.next_frame=now+animation.delays[0];
    image->SelectActiveFrame(&Gdiplus::FrameDimensionTime,0);
}

bool advance_emotion_animation(std::size_t index,ULONGLONG now) {
    auto& animation=emotion_animations[index];
    if (animation.delays.empty() || now<animation.next_frame) { return false; }
    UINT old_frame=animation.frame;

    animation.next_frame+=(now-animation.next_frame)/animation.cycle*animation.cycle;
    while (now>=animation.next_frame) {
        animation.frame=(animation.frame+1)%animation.delays.size();
        animation.next_frame+=animation.delays[animation.frame];
    }
    if (old_frame==animation.frame) { return false; }
    if (animation.frames.empty() &&
        emotion_images[index]->SelectActiveFrame(&Gdiplus::FrameDimensionTime,animation.frame)!=Gdiplus::Ok) {
        animation=EmotionAnimation{};
        return false;
    }
    return true;
}

void update_emotion_animation_timer(bool resume=false) {
    if (!main_window || !IsWindow(main_window)) { return; }
    KillTimer(main_window,EMOTION_GIF_TIMER);
    if (IsIconic(main_window) || interactive_resize) { return; }
    ULONGLONG now=monotonic_ms(),wait=60000;
    bool animated=false;
    for (auto& animation:emotion_animations) {
        if (animation.delays.empty()) { continue; }
        if (resume) { animation.next_frame=now+animation.delays[animation.frame]; }
        animated=true;
        wait=std::min(wait,animation.next_frame>now ? animation.next_frame-now : 10);
    }
    if (animated) { SetTimer(main_window,EMOTION_GIF_TIMER,static_cast<UINT>(std::max<ULONGLONG>(10,wait)),nullptr); }
}

void load_interface_images() {
    std::wstring root=L":/assets";
    for (int preset=0;preset<5;++preset) {
        std::wostringstream path;
        path<<root<<L"\\chord_emotion\\"<<preset+1;
        emotion_images[static_cast<std::size_t>(preset)]=load_emotion_icon(path.str());
        initialize_emotion_animation(static_cast<std::size_t>(preset),monotonic_ms());
    }
    arrow_image=load_png(root+L"\\arrow.png");
}

void draw_square_icon(Gdiplus::Graphics& graphics,Gdiplus::Image* image,
    const Gdiplus::RectF& destination) {
    for (std::size_t i=0;i<emotion_images.size();++i) {
        if (image==emotion_images[i].get() && !emotion_animations[i].frames.empty()) {
            image=emotion_animations[i].frames[emotion_animations[i].frame].get();
            break;
        }
    }
    float side=static_cast<float>(std::min(image->GetWidth(),image->GetHeight()));
    graphics.DrawImage(image,destination,(image->GetWidth()-side)*0.5f,
        (image->GetHeight()-side)*0.5f,side,side,Gdiplus::UnitPixel);
}


void unload_interface_font() {
    interface_font_family.reset();
    interface_font_collection.reset();
    if (registered_font) {
        RemoveFontMemResourceEx(registered_font);
        registered_font=nullptr;
    }
    interface_font_name=L"Microsoft YaHei UI";
}

bool load_interface_font(const std::wstring& path) {
    unload_interface_font();
    std::unique_ptr<Gdiplus::PrivateFontCollection> collection(new Gdiplus::PrivateFontCollection);
    auto bytes=ncnl::embedded_asset(path);
    if (!bytes.data || collection->AddMemoryFont(bytes.data,bytes.size)!=Gdiplus::Ok) { return false; }
    Gdiplus::FontFamily family;
    INT found=0;
    if (collection->GetFamilies(1,&family,&found)!=Gdiplus::Ok || !found) { return false; }
    wchar_t name[LF_FACESIZE]={};
    if (family.GetFamilyName(name)!=Gdiplus::Ok) { return false; }
    DWORD count=0; registered_font=AddFontMemResourceEx(const_cast<BYTE*>(bytes.data),bytes.size,nullptr,&count);
    if (!registered_font) { return false; }
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

    for (const auto& control:controls) {
        HFONT font=new_normal;
        if (control.font_kind==FontKind::title) {
            font=new_title;
        }
        else if (control.font_kind==FontKind::card) {
            font=new_card;
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
    title_font=new_title;
    normal_font=new_normal;
    card_font=new_card;
}

void apply_layout(HWND window) {
    resize_plugin_page(window);
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
        positions=DeferWindowPos(
            positions,control.window,nullptr,x,y,width,visible_height,
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
    FontKind font_kind=FontKind::normal
) {
    HWND control=CreateWindowExW(
        0,class_name,text,style,
        x,y,width,height,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),nullptr
    );
    controls.push_back({control,x,y,width,height,font_kind});
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
    float row=ROLL_GRID_HEIGHT/(roll_high_pitch-roll_low_pitch+1);
    float center=ROLL_GRID_TOP+(roll_high_pitch-pitch+0.5f)*row;
    if (is_black_key(pitch)) {
        return Gdiplus::RectF(50,center-row*0.44f,53,row*0.88f);
    }
    int above=pitch+1,below=pitch-1;
    while (above<=127 && is_black_key(above)) { ++above; }
    while (below>=0 && is_black_key(below)) { --below; }
    float top=std::max(ROLL_GRID_TOP,center-(above-pitch)*row*0.5f);
    float bottom=std::min(645.0f,center+(pitch-below)*row*0.5f);
    return Gdiplus::RectF(50,top,82,std::max(0.0f,bottom-top));
}

int keyboard_pitch_at_client_point(HWND window,POINT point) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    if (x<50 || x>=132 || y<ROLL_GRID_TOP || y>=645) { return -1; }

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
    double bottom=header_only ? ROLL_GRID_TOP : 645.0;
    if (logical_y<ROLL_TOP || logical_y>bottom ||
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
    const double grid_top=ROLL_GRID_TOP;
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
            SetCursor(mouse_feedback.cursor(ncnl::AppCursor::link));
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
                if (slot_presets[target]!=selected_preset) { remember_edit(); }
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

        UINT count=midi_output_api.count();
        for (UINT id=0;id<count && result!=MMSYSERR_NOERROR;++id) {
            opened=nullptr;
            result=midi_output_api.open(&opened,id,0,0,CALLBACK_NULL);
        }
    }
    if (result==MMSYSERR_NOERROR) {
        shared_midi_output=opened;
        send_midi_message(opened,0xC0u);
        send_midi_message(opened,0xC1u);
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
        RECT area={static_cast<LONG>(50*current_scale),static_cast<LONG>(ROLL_GRID_TOP*current_scale),
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

    send_midi_message(data->output,0xC0u);
    auto schedule=ncnl::midi_playback_schedule(data->rhythm);
    double beat_ms=60000.0/std::max(1,data->bpm);
    double loop_ms=data->rhythm.length*beat_ms;
    std::uint64_t loop=0;
    bool stopping=false;
    while (!stopping) {
        for (const auto& note:schedule) {
            double deadline=data->start_ms+loop*loop_ms+note.time/ncnl::PLAYBACK_UNITS_PER_BEAT*beat_ms;
            stopping=wait_midi_deadline(data->stop_event,deadline);
            if (stopping) { break; }
            send_midi_note(data->output,note.pitch,note.velocity,note.on);
        }
        if (!stopping) {
            stopping=wait_midi_deadline(data->stop_event,data->start_ms+(loop+1)*loop_ms);
        }
        ++loop;
    }

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


            constraints[position].fixed_notes=all_chords_filled
                ? ""
                : displayed_progression.chords[position].notes;
        }

        auto generated=ncnl::generate_progression(
            current_mode,constraints
        );
        remember_edit();
        displayed_progression=std::move(generated);
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

void undo_last_edit() {
    if (undo_history.empty()) { return; }
    stop_midi_playback();
    stop_key_preview();
    discard_note_effects();
    hide_chord_editor();
    dragged_midi_position=dragged_midi_event=dragged_midi_pitch=-1;
    midi_drag_recorded=false;
    if (GetCapture()) { ReleaseCapture(); }
    dragged_preset=drag_hover_slot=-1;
    preset_dragging=false;
    auto saved=std::move(undo_history.back());
    undo_history.pop_back();
    rhythm=std::move(saved.rhythm);
    rhythm_splits=std::move(saved.splits);
    chord_blocks=std::move(saved.blocks);
    displayed_progression=std::move(saved.progression);
    slot_presets=std::move(saved.presets);
    rhythm_file_name=std::move(saved.file_name);
    active_slot=saved.active; roll_low_pitch=saved.low; roll_high_pitch=saved.high;
    update_slot_ui();
    RedrawWindow(main_window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
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
    if (x<132 || x>=950 || y<ROLL_GRID_TOP || y>=645) { return false; }
    int rows=roll_high_pitch-roll_low_pitch+1;
    int pitch=roll_high_pitch-std::min(rows-1,static_cast<int>((y-ROLL_GRID_TOP)/(ROLL_GRID_HEIGHT/rows)));
    double beat=(x-132)/818*rhythm.length;

    for (std::size_t i=0;i<rhythm.events.size();++i) {
        auto& event=rhythm.events[i];
        if (beat>=event.start && beat<event.start+event.duration) {
            if (std::find(event.pitches.begin(),event.pitches.end(),pitch)==event.pitches.end()) {
                remember_edit();
                stop_midi_playback();
                event.pitches.push_back(pitch);
                active_slot=static_cast<int>(event_block(i));
                sync_block_from_events(event_block(i));
                refresh_progression_display();
            }
            return true;
        }
    }

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
    remember_edit();
    rhythm.events.insert(rhythm.events.begin()+insertion,{start,end-start,96,{pitch}});

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
            auto erased=arrow_bounds(i+1);
            merge_rhythm_boundary(i);
            start_arrow_dissolve(erased);
            return;
        }
    }
    int position=slot_at_client_point(window,point);
    if (position>=0) {
        if (slot_presets[position]!=-1) {
            remember_edit();
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
            remember_edit();
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
    remember_edit();
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
    midi_drag_recorded=false;
    stop_midi_playback();
    SetCapture(window);
    SetCursor(mouse_feedback.cursor(ncnl::AppCursor::vertical));
    return true;
}

void update_midi_note_drag(HWND window,POINT point) {
    if (dragged_midi_position<0 || dragged_midi_pitch<0) {
        return;
    }
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    int rows=roll_high_pitch-roll_low_pitch+1;
    int lane=std::max(0,std::min(rows-1,static_cast<int>((y-ROLL_GRID_TOP)/(ROLL_GRID_HEIGHT/rows))));
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
    if (!midi_drag_recorded) { remember_edit(); midi_drag_recorded=true; }
    *source=target_pitch;
    sync_block_from_events(static_cast<std::size_t>(dragged_midi_position));
    dragged_midi_pitch=target_pitch;
    refresh_progression_display();
    SetCursor(mouse_feedback.cursor(ncnl::AppCursor::vertical));
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
    SetCursor(mouse_feedback.cursor(ncnl::AppCursor::link));
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
        presets.push_back(block.first==chord_blocks[old].first ? slot_presets[old] : -1);
    }
    chord_blocks=std::move(blocks);
    displayed_progression.chords=std::move(chords);
    slot_presets=std::move(presets);
    active_slot=std::min(active_slot,static_cast<int>(chord_blocks.size())-1);
}

void merge_rhythm_boundary(std::size_t boundary) {
    if (boundary>=rhythm_splits.size() || !rhythm_splits[boundary]) { return; }
    remember_edit();
    stop_midi_playback();
    hide_chord_editor();
    rhythm_splits[boundary]=false;
    rebuild_chord_blocks();
    std::size_t block=event_block(boundary);
    if (!displayed_progression.chords[block].notes.empty()) { apply_block_notes(block); }
    refresh_progression_display();
}

void toggle_rhythm_split(HWND window,POINT point,bool sweep=false) {
    double x=0,y=0;
    piano_logical_point(window,point,x,y);
    if (x<132.0 || x>950.0 || y<ROLL_TOP || y>725.0 || rhythm.events.size()<2) { return; }
    double nearest=15.0;
    int boundary=-1;
    for (std::size_t i=1;i<rhythm.events.size();++i) {
        double distance=std::abs(x-timeline_x(event_boundary(i)));
        if (distance<nearest) { nearest=distance; boundary=static_cast<int>(i)-1; }
    }
    if (boundary<0) { return; }
    if (middle_dragging && !middle_visited.insert(static_cast<std::size_t>(boundary)).second) { return; }
    if (rhythm_splits[static_cast<std::size_t>(boundary)]) {
        if (sweep) { return; }
        merge_rhythm_boundary(static_cast<std::size_t>(boundary));
        return;
    }
    stop_midi_playback();
    hide_chord_editor();
    remember_edit();
    rhythm_splits[static_cast<std::size_t>(boundary)]=true;
    rebuild_chord_blocks();
    refresh_progression_display();
}

void sweep_rhythm_splits(HWND window,POINT point) {
    double x=0,y=0,previous_x=0,previous_y=0;
    piano_logical_point(window,point,x,y);
    piano_logical_point(window,middle_previous,previous_x,previous_y);
    if (y>=ROLL_TOP && y<=725 && previous_y>=ROLL_TOP && previous_y<=725) {
        for (std::size_t i=1;i<rhythm.events.size();++i) {
            double boundary=timeline_x(event_boundary(i));
            if (boundary>=std::min(x,previous_x)-2 && boundary<=std::max(x,previous_x)+2) {
                POINT at={static_cast<LONG>(boundary*current_scale),point.y};
                toggle_rhythm_split(window,at,true);
            }
        }
    }
    toggle_rhythm_split(window,point,true);
    middle_previous=point;
}

HCURSOR application_cursor(HWND target,int hit,bool right_down) {
    if (right_down) { return mouse_feedback.cursor(ncnl::AppCursor::unavailable); }
    if (hit==HTTOP || hit==HTBOTTOM || hit==HTTOPLEFT || hit==HTTOPRIGHT ||
        hit==HTBOTTOMLEFT || hit==HTBOTTOMRIGHT || dragged_midi_position>=0) {
        return mouse_feedback.cursor(ncnl::AppCursor::vertical);
    }
    if (target==chord_editor) { return mouse_feedback.cursor(ncnl::AppCursor::text); }
    return mouse_feedback.cursor(ncnl::AppCursor::link);
}

void track_mouse_feedback_message(const MSG& message) {
    UINT type=message.message;
    if (type!=WM_MOUSEMOVE && type!=WM_LBUTTONDOWN && type!=WM_LBUTTONDBLCLK &&
        type!=WM_MBUTTONDOWN && type!=WM_RBUTTONDOWN && type!=WM_LBUTTONUP &&
        type!=WM_MBUTTONUP && type!=WM_RBUTTONUP) { return; }
    POINT point={GET_X_LPARAM(message.lParam),GET_Y_LPARAM(message.lParam)};
    bool configuration=configuration_window && GetAncestor(message.hwnd,GA_ROOT)==configuration_window;
    bool presets=ncnl::preset_library_window() && GetAncestor(message.hwnd,GA_ROOT)==ncnl::preset_library_window();
    HWND host=presets ? ncnl::preset_library_window() : configuration ? configuration_window : main_window;
    MapWindowPoints(message.hwnd,host,&point,1);
    RECT client{}; GetClientRect(host,&client);
    float scale=presets ? static_cast<float>(client.right)/DESIGN_WIDTH : configuration ? static_cast<float>(client.right)/SETTINGS_DESIGN_SIZE
        : static_cast<float>(current_scale);
    auto& feedback=presets ? ncnl::preset_library_feedback() : configuration ? configuration_mouse_feedback : mouse_feedback;
    feedback.input(point,static_cast<UINT>(message.wParam),
        type==WM_LBUTTONDOWN || type==WM_LBUTTONDBLCLK,scale,monotonic_ms());
    update_animation_clock();
    if (presets) { SendMessageW(host,WM_SETCURSOR,reinterpret_cast<WPARAM>(message.hwnd),MAKELPARAM(HTCLIENT,type)); }
    else { SetCursor(application_cursor(message.hwnd,HTCLIENT,(message.wParam&MK_RBUTTON)!=0)); }
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

bool import_midi_file(HWND owner,const std::wstring& path) {
    try {
        auto imported=ncnl::parse_midi_rhythm(read_midi_file(path));
        remember_edit();
        stop_midi_playback();
        discard_note_effects();
        hide_chord_editor();
        rhythm=std::move(imported);
        rhythm_splits.assign(rhythm.events.size()-1,false);
        chord_blocks={{0,rhythm.events.size()}};
        displayed_progression.chords.assign(1,{"",0.0});
        displayed_progression.quality_score=0.0;
        slot_presets.assign(1,-1);
        active_slot=0;
        drag_hover_slot=-1;
        dragged_midi_position=-1;
        dragged_midi_event=-1;
        dragged_midi_pitch=-1;
        std::size_t slash=path.find_last_of(L"\\/");
        rhythm_file_name=path.substr(slash==std::wstring::npos ? 0 : slash+1);
        update_pitch_range();
        update_slot_ui();
        return true;
    }
    catch (const std::exception& error) {
        MessageBoxW(owner,utf8_to_wide(error.what()).c_str(),L"无法导入 MIDI",MB_OK|MB_ICONERROR);
        return false;
    }
}


void cancel_header_drag(HWND window,bool release=false) {
    bool pending=header_drag_block>=0;
    header_drag_block=-1; header_drag_started=0;
    KillTimer(window,HEADER_DRAG_TIMER);
    if (release && pending && GetCapture()==window) { ReleaseCapture(); }
}

void continue_header_drag(HWND window) {
    if (header_drag_block<0 || monotonic_ms()-header_drag_started<HEADER_DRAG_HOLD_MS) { return; }
    if (std::abs(header_drag_pointer.x-header_drag_press.x)<std::max(4,GetSystemMetrics(SM_CXDRAG)) &&
        std::abs(header_drag_pointer.y-header_drag_press.y)<std::max(4,GetSystemMetrics(SM_CYDRAG))) { return; }
    std::size_t block=static_cast<std::size_t>(header_drag_block);
    cancel_header_drag(window,true);
    mouse_feedback.clear(); update_animation_clock();
    if (block>=chord_blocks.size()) { return; }
    try {

        if (!std::any_of(rhythm.events.begin(),rhythm.events.end(),
            [](const ncnl::RhythmEvent& event){ return !event.pitches.empty(); })) { return; }
        auto bytes=ncnl::encode_midi(rhythm,BPM);
        std::wstring name=utf8_to_wide(current_mode)+L"_奇妙的和弦进行";
        DWORD effect=DROPEFFECT_NONE;
        HRESULT result=ncnl::drag_midi_file(bytes,name,&effect,nullptr);
        if (FAILED(result)) {
            MessageBoxW(window,L"无法创建 MIDI 拖出文件或启动 Windows 文件拖放。",L"无法导出 MIDI",MB_OK|MB_ICONERROR);
        }
    } catch (const std::exception& error) {
        MessageBoxW(window,utf8_to_wide(error.what()).c_str(),L"无法导出 MIDI",MB_OK|MB_ICONERROR);
    }
}

void begin_header_drag(HWND window,int block,POINT point) {
    cancel_header_drag(window);
    SetFocus(window); SetCapture(window);
    header_drag_block=block; header_drag_press=point; header_drag_pointer=point;
    header_drag_started=monotonic_ms();
    SetTimer(window,HEADER_DRAG_TIMER,30,nullptr);
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
            remember_edit();
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
            remember_edit();
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
    int y=offset_y+static_cast<int>(std::lround((ROLL_TOP+5.0)*scale));
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
    float scale,
    bool draw_emotion_images=true
) {
    static const std::array<const wchar_t*,12> pitch_names={{
        L"C",L"C#",L"D",L"D#",L"E",L"F",
        L"F#",L"G",L"G#",L"A",L"A#",L"B"
    }};
    const float left=offset_x+50.0f*scale;
    const float top=offset_y+ROLL_TOP*scale;
    const float roll_width=900.0f*scale;
    const float roll_height=(645.0f-ROLL_TOP)*scale;
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
            if (draw_emotion_images) {
                draw_square_icon(graphics,emotion_images[static_cast<std::size_t>(preset)].get(),bounds);
            }
        }
        else { draw_centered_text(graphics,L"＋",bounds,std::min(28.0f,logical.Width*0.4f)*scale); }
        graphics.Restore(saved);
        Gdiplus::Pen edge(block==static_cast<std::size_t>(active_slot)
            ? Gdiplus::Color(255,139,234,247) : Gdiplus::Color(150,122,155,192),
            block==static_cast<std::size_t>(active_slot) ? 2.5f*scale : scale);
        graphics.DrawPath(&edge,&path);
        std::wostringstream label;
        label<<utf8_to_wide(ncnl::emotion_preset_label(preset));
        float left=static_cast<float>(timeline_x(block_start(block)));
        float cell=static_cast<float>(timeline_x(block_end(block)))-left;
        draw_centered_text(graphics,label.str(),
            Gdiplus::RectF(offset_x+left*scale,
                offset_y+(SLOT_Y+logical.Height+12.0f)*scale,cell*scale,24.0f*scale),
            std::min(22.0f,cell*0.20f)*scale,true);
    }
}

int32_t NCNL_CALL plugin_context(void*,NcnlContextV1* context) {
    if (!context || context->size<sizeof(NcnlContextV1)) { return 0; }
    *context={}; context->size=sizeof(*context); context->bpm=BPM;
    context->rhythm_events=static_cast<uint32_t>(rhythm.events.size());
    for (const auto& event:rhythm.events) { context->midi_notes+=static_cast<uint32_t>(event.pitches.size()); }
    std::strncpy(context->mode,current_mode.c_str(),sizeof(context->mode)-1); return 1;
}
uint32_t NCNL_CALL plugin_read_midi(void*,uint8_t* output,uint32_t capacity) {
    try {
        auto bytes=ncnl::encode_midi(rhythm,BPM);
        if (output && capacity>=bytes.size()) { std::copy(bytes.begin(),bytes.end(),output); }
        return static_cast<uint32_t>(bytes.size());
    } catch (...) { return 0; }
}
Gdiplus::RectF plugin_button_bounds(int index,int count) {
    float width=216,gap=12,total=count*width+(count-1)*gap;
    return {(1000-total)*0.5f+index*(width+gap),934,width,46};
}
void draw_plugin_buttons(Gdiplus::Graphics& graphics,float offset_x,float offset_y,float scale) {
    auto saved=graphics.Save(); graphics.TranslateTransform(offset_x,offset_y); graphics.ScaleTransform(scale,scale);
    auto indices=plugin_manager.enabled_plugins();
    for (std::size_t i=0;i<indices.size();++i) {
        auto box=plugin_button_bounds(static_cast<int>(i),static_cast<int>(indices.size()));
        auto index=indices[i]; bool selected=plugin_manager.active_index()==index;
        ncnl::ButtonArtwork* art=index==0 ? &ncnl_button_art :
            (index<plugin_button_art.size() ? plugin_button_art[index].get() : nullptr);
        if (art && *art) {
            auto size=art->visible_size(); float height=box.Width*size.Height/size.Width;
            if (height<=box.Height) { box.Y+=(box.Height-height)*0.5f; box.Height=height; }
            if (art->draw(graphics,box,selected)) { continue; }
        }
        Gdiplus::GraphicsPath shape; add_rounded_rectangle(shape,box,12);
        Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(box.X,box.Y),Gdiplus::PointF(box.X,box.GetBottom()),
            selected?Gdiplus::Color(255,124,175,145):Gdiplus::Color(245,54,77,85),Gdiplus::Color(255,35,65,69));
        Gdiplus::Pen edge(Gdiplus::Color(230,177,218,191),1.4f); graphics.FillPath(&fill,&shape); graphics.DrawPath(&edge,&shape);
        draw_centered_text(graphics,plugin_manager.plugins()[index].name,box,20,true);
    }
    graphics.Restore(saved);
}
void resize_plugin_page(HWND window) {
    if (!plugin_container) { return; }
    RECT client={}; GetClientRect(window,&client);
    double scale=std::min(client.right/1000.0,client.bottom/1000.0);
    int width=static_cast<int>(900*scale),height=static_cast<int>(780*scale);
    MoveWindow(plugin_container,static_cast<int>(50*scale),static_cast<int>(120*scale),width,height,TRUE);
    plugin_manager.resize(width,height);
}
void update_plugin_interface(HWND window) {
    bool builtin=plugin_manager.active_index()==0;
    for (const auto& control:controls) {
        if (control.window==settings_button || control.window==mode_label) { continue; }
        ShowWindow(control.window,builtin?SW_SHOW:SW_HIDE);
    }
    if (plugin_container) { ShowWindow(plugin_container,builtin?SW_HIDE:SW_SHOW); }
    resize_plugin_page(window); main_background_dirty=true;
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void refresh_plugins(HWND window) {
    std::wstring error;
    NcnlHostV1 host={sizeof(NcnlHostV1),NCNL_PLUGIN_ABI,nullptr,plugin_context,plugin_read_midi};
    plugin_manager.activate(0,plugin_container,host,error);
    try {
        plugin_manager.scan(executable_directory()+L"\\plugins");
        plugin_button_art.clear(); plugin_button_art.resize(plugin_manager.plugins().size());
        for (std::size_t i=1;i<plugin_button_art.size();++i) {
            plugin_button_art[i].reset(new ncnl::ButtonArtwork);
            plugin_button_art[i]->load(plugin_manager.plugins()[i].button);
        }
    } catch (const std::exception& error) { MessageBoxW(window,utf8_to_wide(error.what()).c_str(),L"插件列表读取失败",MB_OK|MB_ICONWARNING); }
    plugin_scroll=0; configuration_cached_tab=-1; update_plugin_interface(window);
}
bool switch_plugin_at(HWND window,POINT point) {
    RECT client={}; GetClientRect(window,&client); float scale=static_cast<float>(std::min(client.right/1000.0,client.bottom/1000.0));
    if (scale<=0) { return false; }
    auto indices=plugin_manager.enabled_plugins();
    for (std::size_t i=0;i<indices.size();++i) {
        if (!plugin_button_bounds(static_cast<int>(i),static_cast<int>(indices.size())).Contains(point.x/scale,point.y/scale)) { continue; }
        cancel_header_drag(window,true); hide_chord_editor(); stop_key_preview(); dissolving_notes.clear();
        NcnlHostV1 host={sizeof(NcnlHostV1),NCNL_PLUGIN_ABI,nullptr,plugin_context,plugin_read_midi};
        std::wstring error;
        if (!plugin_manager.activate(indices[i],plugin_container,host,error)) {
            MessageBoxW(window,error.c_str(),L"插件无法加载",MB_OK|MB_ICONERROR);
        }
        configuration_cached_tab=-1; update_plugin_interface(window); return true;
    }
    return false;
}
void draw_background(HWND window,HDC dc,bool draw_details,bool draw_emotion_images=true) {
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


    if (!draw_details) {
        return;
    }

    background_image.draw(graphics,width,height,0.40f);

    double scale=std::max(0.25,std::min(
        width/static_cast<double>(DESIGN_WIDTH),
        height/static_cast<double>(DESIGN_HEIGHT)
    ));
    float offset_x=static_cast<float>((width-DESIGN_WIDTH*scale)/2.0);
    float offset_y=static_cast<float>((height-DESIGN_HEIGHT*scale)/2.0);
    if (plugin_manager.active_index()==0) { draw_piano_roll(
        graphics,offset_x,offset_y,static_cast<float>(scale),draw_emotion_images
    ); }
    draw_plugin_buttons(graphics,offset_x,offset_y,static_cast<float>(scale));
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
        draw_background(window,main_background_dc,!interactive_resize,false);
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

    float u=(spark+particle_random(seed+spark*67))/count;
    float v=particle_random(seed+spark*79);
    return Gdiplus::PointF(bounds.X+bounds.Width*u+std::cos(angle)*distance,
        bounds.Y+bounds.Height*v+std::sin(angle)*distance);
}

void draw_light_particle(Gdiplus::Graphics& graphics,float x,float y,
    float radius,float opacity,bool violet,bool star,bool arrow=false) {
    if (x<122 || x>960 || y<(arrow ? 645 : ROLL_GRID_TOP-10) || y>(arrow ? 735 : 655)) { return; }
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
    if (plugin_manager.active_index()!=0) { return; }
    if (dissolving_notes.empty()) { return; }
    Gdiplus::Graphics graphics(dc);
    graphics.ScaleTransform(static_cast<float>(current_scale),static_cast<float>(current_scale));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath clip;
    add_rounded_rectangle(clip,Gdiplus::RectF(50,ROLL_TOP,900,645.0f-ROLL_TOP),18);
    ULONGLONG now=monotonic_ms();
    for (const auto& note:dissolving_notes) {
        float age=static_cast<float>(now-note.born)/DISSOLVE_DURATION_MS;
        if (age>=1.0f) { continue; }
        auto saved=graphics.Save();
        if (note.arrow) { graphics.SetClip(Gdiplus::RectF(132,645,818,90)); }
        else {
            graphics.SetClip(&clip);
            graphics.SetClip(Gdiplus::RectF(132,ROLL_GRID_TOP,818,ROLL_GRID_HEIGHT),Gdiplus::CombineModeIntersect);
        }
        const auto& box=note.bounds;
        float fade=(1.0f-age)*(1.0f-age);
        float drift=1.0f-std::pow(1.0f-age,3.0f);

        if (note.arrow && arrow_image && age<0.5f) {
            Gdiplus::ColorMatrix matrix={1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1-age*2,0,0,0,0,0,1};
            Gdiplus::ImageAttributes attributes; attributes.SetColorMatrix(&matrix);
            graphics.DrawImage(arrow_image.get(),box,0,0,
                static_cast<float>(arrow_image->GetWidth()),static_cast<float>(arrow_image->GetHeight()),
                Gdiplus::UnitPixel,&attributes);
        }
        else if (!note.arrow && age<0.22f) {
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
            float radius=(note.arrow ? 1.2f+particle_random(note.seed+i*71)*2.0f
                : 0.8f+particle_random(note.seed+i*71)*1.4f)*(1.0f-0.6f*age);
            Gdiplus::Pen trail(Gdiplus::Color(static_cast<BYTE>(75*fade),154,225,255),0.6f);
            graphics.DrawLine(&trail,x-vx*age*0.13f,y-vy*age*0.13f,x,y);
            draw_light_particle(graphics,x,y,radius,(note.arrow ? 255 : 215)*fade,
                note.alternate,i%(note.arrow ? 4 : 7)==0,note.arrow);
        }
        graphics.Restore(saved);
    }
}

void draw_playback_overlay(HDC dc) {
    if (plugin_manager.active_index()!=0) { return; }
    if (playback_loop_ms<=0.0) { return; }
    double elapsed=static_cast<double>(monotonic_ms()-playback_start_ms);
    double beat=std::fmod(elapsed,playback_loop_ms)/playback_loop_ms*rhythm.length;
    float scale=static_cast<float>(current_scale);
    Gdiplus::Graphics graphics(dc);
    graphics.ScaleTransform(scale,scale);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath roll_clip;
    add_rounded_rectangle(roll_clip,Gdiplus::RectF(50,ROLL_TOP,900,645.0f-ROLL_TOP),18);
    graphics.SetClip(&roll_clip);
    graphics.SetClip(Gdiplus::RectF(132,ROLL_GRID_TOP,818,ROLL_GRID_HEIGHT),Gdiplus::CombineModeIntersect);
    float x=static_cast<float>(timeline_x(beat));
    for (int glow=3;glow>=1;--glow) {
        Gdiplus::Pen pen(Gdiplus::Color(glow==1?235:22,153,228,255),
            glow==1?1.5f:glow*5.0f);
        graphics.DrawLine(&pen,x,ROLL_GRID_TOP,x,645.0f);
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

void draw_emotion_overlay(HDC dc,const RECT& dirty) {
    if (plugin_manager.active_index()!=0) { return; }
    Gdiplus::Graphics graphics(dc);
    graphics.SetClip(Gdiplus::Rect(dirty.left,dirty.top,dirty.right-dirty.left,dirty.bottom-dirty.top));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    float scale=static_cast<float>(current_scale);
    for (std::size_t block=0;block<chord_blocks.size();++block) {
        int preset=slot_presets[block];
        if (preset<0 || !emotion_images[static_cast<std::size_t>(preset)]) { continue; }
        auto box=emotion_bounds(block);
        Gdiplus::RectF bounds(box.X*scale,box.Y*scale,box.Width*scale,box.Height*scale);
        if (bounds.GetRight()<dirty.left || bounds.X>dirty.right ||
            bounds.GetBottom()<dirty.top || bounds.Y>dirty.bottom) { continue; }
        Gdiplus::GraphicsPath path;
        add_rounded_rectangle(path,bounds,std::min(16.0f,box.Width*0.15f)*scale);
        auto saved=graphics.Save(); graphics.SetClip(&path,Gdiplus::CombineModeIntersect);
        draw_square_icon(graphics,emotion_images[static_cast<std::size_t>(preset)].get(),bounds);
        graphics.Restore(saved);
        Gdiplus::Pen edge(block==static_cast<std::size_t>(active_slot)
            ? Gdiplus::Color(255,139,234,247) : Gdiplus::Color(150,122,155,192),
            block==static_cast<std::size_t>(active_slot) ? 2.5f*scale : scale);
        graphics.DrawPath(&edge,&path);
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
        if (!interactive_resize) {
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
                RECT roll={static_cast<LONG>(132*current_scale),static_cast<LONG>(ROLL_GRID_TOP*current_scale),
                    static_cast<LONG>(951*current_scale),static_cast<LONG>(646*current_scale)},intersection{};
                if (IntersectRect(&intersection,&dirty,&roll)) {
                    draw_playback_overlay(animation_dc);
                }
                draw_dissolve_overlay(animation_dc);
                draw_emotion_overlay(animation_dc,dirty);
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

void draw_square_button_content(const DRAWITEMSTRUCT* item) {
    RECT bounds=item->rcItem;
    bool pressed=(item->itemState&ODS_SELECTED)!=0;
    int id=static_cast<int>(item->CtlID);



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

    if (id==ID_GENERATE || id==ID_SETTINGS || id==ID_PRESET_ACTION) {
        ncnl::ButtonArt art=id==ID_GENERATE ? ncnl::ButtonArt::Generate :
            id==ID_SETTINGS ? ncnl::ButtonArt::Settings : ncnl::ButtonArt::Presets;
        Gdiplus::Graphics graphics(item->hDC);
        if (button_artworks.draw(graphics,art,Gdiplus::RectF(static_cast<float>(bounds.left),static_cast<float>(bounds.top),
            static_cast<float>(bounds.right-bounds.left),static_cast<float>(bounds.bottom-bounds.top)),false,pressed)) { return; }
    }

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

    if (button_image || (id>=ID_PRESET_BASE && id<ID_PRESET_BASE+5)) {
        Gdiplus::Graphics graphics(item->hDC);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
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
        Gdiplus::LinearGradientBrush image_background(
            Gdiplus::PointF(0,0),Gdiplus::PointF(static_cast<float>(image_width),static_cast<float>(image_height)),
            Gdiplus::Color(255,std::min(255,GetRValue(fill)+55),std::min(255,GetGValue(fill)+45),std::min(255,GetBValue(fill)+35)),
            Gdiplus::Color(255,GetRValue(fill)*2/3,GetGValue(fill)*2/3,GetBValue(fill)*2/3));
        graphics.FillPath(&image_background,&clip_path);
        if (button_image) {
            Gdiplus::RectF icon(static_cast<float>(bounds.left),static_cast<float>(bounds.top),
                static_cast<float>(image_width),static_cast<float>(image_height));
            draw_square_icon(graphics,button_image,icon);
        }
        else {
            draw_centered_text(graphics,utf8_to_wide(ncnl::emotion_preset_label(id-ID_PRESET_BASE)),
                Gdiplus::RectF(0,0,static_cast<float>(image_width),static_cast<float>(image_height)),
                16.0f*static_cast<float>(current_scale));
        }
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

void draw_square_button(const DRAWITEMSTRUCT* item) {

    int width=item->rcItem.right-item->rcItem.left;
    int height=item->rcItem.bottom-item->rcItem.top;
    HDC buffer=CreateCompatibleDC(item->hDC);
    HBITMAP bitmap=CreateCompatibleBitmap(item->hDC,width,height);
    if (!buffer || !bitmap) {
        if (bitmap) { DeleteObject(bitmap); }
        if (buffer) { DeleteDC(buffer); }
        draw_square_button_content(item);
        return;
    }
    HGDIOBJ old=SelectObject(buffer,bitmap);
    DRAWITEMSTRUCT buffered=*item;
    buffered.hDC=buffer; buffered.rcItem={0,0,width,height};
    draw_square_button_content(&buffered);
    BitBlt(item->hDC,item->rcItem.left,item->rcItem.top,width,height,buffer,0,0,SRCCOPY);
    SelectObject(buffer,old); DeleteObject(bitmap); DeleteDC(buffer);
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


    Gdiplus::PointF(398.0f,100.0f),
    Gdiplus::PointF(675.0f,285.0f),
    Gdiplus::PointF(587.0f,639.0f),



    Gdiplus::PointF(230.0f,612.0f),

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

float configuration_progress(ULONGLONG start,int duration) {
    if (!start) { return 1; }
    float t=std::min(1.0f,static_cast<float>(monotonic_ms()-start)/duration);
    return t*t*(3-2*t);
}
void animate_configuration(HWND window) {
    configuration_animating=true; update_animation_clock();
    InvalidateRect(window,nullptr,FALSE);
}
int wheel_top_index() {
    int index=static_cast<int>(std::round(-wheel_rotation/30));
    return (index%12+12)%12;
}
void rotate_wheel_to(HWND window,int index) {
    index=(index%12+12)%12; pending_tonic=FIFTHS[index];
    wheel_from=wheel_rotation; wheel_target=-index*30.0;
    wheel_target+=360*std::round((wheel_from-wheel_target)/360);
    wheel_started=monotonic_ms(); animate_configuration(window);
}
void initialize_mode_wheel() {
    auto mode=ncnl::parse_mode(current_mode); pending_tonic=mode.tonic; pending_mode=0;
    for (int i=0;i<7;++i) { if (mode.name==MODE_NAMES[i]) { pending_mode=i; } }
    for (int i=0;i<12;++i) { if (FIFTHS[i]==pending_tonic) { wheel_rotation=-i*30.0; } }
    wheel_from=wheel_target=wheel_rotation; wheel_started=0; wheel_dragging=false;
}
std::string pending_mode_text() { return std::string(TONIC_NAMES[pending_tonic])+" "+MODE_NAMES[pending_mode]; }
void apply_selected_mode() {
    stop_midi_playback(); stop_key_preview();
    current_mode=pending_mode_text();
    SetWindowTextW(mode_label,(L"当前调式："+utf8_to_wide(current_mode)).c_str());
    for (auto& chord:displayed_progression.chords) {
        if (!chord.notes.empty()) { chord.emotion_score=ncnl::roll_emotion_score(current_mode,chord.notes); }
    }
    recalculate_progression_quality(); update_slot_ui();
}
double wheel_angle(const Gdiplus::PointF& p) { return std::atan2(p.Y-WHEEL_Y,p.X-WHEEL_X)*180/3.141592653589793; }
void draw_wheel_ring(Gdiplus::Graphics& graphics,float scale_factor) {
    if (std::abs(wheel_glyph_scale-scale_factor)>0.001f) {
        wheel_glyph_scale=scale_factor;
        for (int pitch=0;pitch<12;++pitch) { for (int style=0;style<3;++style) {
            auto& glyph=wheel_glyphs[pitch*3+style];
            glyph.reset(new Gdiplus::Bitmap(static_cast<INT>(std::ceil(80*scale_factor)),
                static_cast<INT>(std::ceil(58*scale_factor)),PixelFormat32bppPARGB));
            Gdiplus::Graphics cached(glyph.get()); cached.Clear(Gdiplus::Color(0,0,0,0));
            cached.ScaleTransform(scale_factor,scale_factor);
            draw_centered_text(cached,utf8_to_wide(TONIC_NAMES[pitch]),{0,0,80,58},style==2 ? 38 : 31,style!=0,
                style==0 ? Gdiplus::Color(255,245,248,255) : Gdiplus::Color(255,0,0,0));
        } }
    }
    auto scale=ncnl::parse_mode(pending_mode_text());
    Gdiplus::Pen outline(Gdiplus::Color(220,143,191,209),1.5f);
    for (int i=0;i<12;++i) {
        int pitch=FIFTHS[i]; bool active=false;
        for (int interval:scale.intervals) { if ((pending_tonic+interval)%12==pitch) { active=true; } }
        float angle=static_cast<float>(-90+i*30+wheel_rotation);
        Gdiplus::GraphicsPath sector;
        sector.AddArc(WHEEL_X-WHEEL_RADIUS,WHEEL_Y-WHEEL_RADIUS,WHEEL_RADIUS*2,WHEEL_RADIUS*2,angle-14.3f,28.6f);
        sector.AddArc(WHEEL_X-WHEEL_INNER,WHEEL_Y-WHEEL_INNER,WHEEL_INNER*2,WHEEL_INNER*2,angle+14.3f,-28.6f); sector.CloseFigure();
        bool tonic=pitch==pending_tonic;
        Gdiplus::Color fill=tonic ? Gdiplus::Color(255,96,190,158) : active
            ? Gdiplus::Color(255,125+8*i,166+4*i,184+4*i) : Gdiplus::Color(255,43,49,65);
        Gdiplus::SolidBrush brush(fill); graphics.FillPath(&brush,&sector);
        if (active) { graphics.DrawPath(&outline,&sector); }
        double rad=angle*3.141592653589793/180;
        float x=WHEEL_X+static_cast<float>(WHEEL_LABEL_RADIUS*std::cos(rad)),y=WHEEL_Y+static_cast<float>(WHEEL_LABEL_RADIUS*std::sin(rad));
        graphics.DrawImage(wheel_glyphs[pitch*3+(tonic ? 2 : active ? 1 : 0)].get(),Gdiplus::RectF(x-40,y-29,80,58));
    }
    Gdiplus::PointF triangle[3]={{390,12},{410,12},{400,30}};
    Gdiplus::SolidBrush indicator(Gdiplus::Color(255,214,255,235)); graphics.FillPolygon(&indicator,triangle,3);
}
void draw_mode_wheel(Gdiplus::Graphics& graphics,bool ring=true,float scale_factor=1) {
    if (ring) { draw_wheel_ring(graphics,scale_factor); }
    draw_centered_text(graphics,utf8_to_wide(pending_mode_text()),{235,WHEEL_Y-30,330,60},32,true);
    for (int mode=0;mode<7;++mode) {
        int row=mode/4,column=mode%4;
        Gdiplus::RectF box(62.0f+171*column+(row ? 85 : 0),565.0f+49*row,162,40);
        auto art=static_cast<ncnl::ButtonArt>(static_cast<int>(ncnl::ButtonArt::Ionian)+mode);
        if (button_artworks.draw(graphics,art,box,mode==pending_mode)) { continue; }
        Gdiplus::GraphicsPath path; add_rounded_rectangle(path,box,12);
        Gdiplus::SolidBrush fill(mode==pending_mode ? Gdiplus::Color(255,64,137,151) : Gdiplus::Color(255,49,56,76));
        graphics.FillPath(&fill,&path); draw_centered_text(graphics,utf8_to_wide(MODE_NAMES[mode]),box,18,mode==pending_mode);
    }
    Gdiplus::RectF confirm(230,670,340,40); Gdiplus::GraphicsPath path; add_rounded_rectangle(path,confirm,12);
    if (!button_artworks.draw(graphics,ncnl::ButtonArt::Confirm,confirm)) {
        Gdiplus::SolidBrush fill(Gdiplus::Color(255,62,142,117)); graphics.FillPath(&fill,&path);
        draw_centered_text(graphics,L"决定了，就是你！",confirm,20,true);
    }
}

Gdiplus::RectF configuration_tab_bounds(int tab) {
    return {32.0f+248*tab,739,240.0f,55.0f*2/3};
}
void plugin_action_button(Gdiplus::Graphics& graphics,const std::wstring& label,Gdiplus::RectF box,bool accent=false) {
    Gdiplus::GraphicsPath path; add_rounded_rectangle(path,box,12);
    Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(box.X,box.Y),Gdiplus::PointF(box.X,box.GetBottom()),
        accent?Gdiplus::Color(250,79,154,132):Gdiplus::Color(245,55,77,88),Gdiplus::Color(250,29,49,61));
    Gdiplus::Pen edge(Gdiplus::Color(180,153,210,195),1.2f);
    graphics.FillPath(&fill,&path); graphics.DrawPath(&edge,&path); draw_centered_text(graphics,label,box,17,true);
}
void draw_plugin_management(Gdiplus::Graphics& graphics) {
    draw_centered_text(graphics,L"插件管理",{40,28,720,48},30,true);
    const auto& items=plugin_manager.plugins();
    for (int visible=0;visible<4;++visible) {
        int index=plugin_scroll+visible; if (index>=static_cast<int>(items.size())) { break; }
        const auto& info=items[index]; float y=94+visible*136.0f;
        Gdiplus::RectF box(40,y,720,124); Gdiplus::GraphicsPath path; add_rounded_rectangle(path,box,18);
        Gdiplus::SolidBrush fill(Gdiplus::Color(215,info.enabled?35:26,info.enabled?69:43,info.enabled?71:57));
        Gdiplus::Pen edge(Gdiplus::Color(120,140,205,185),1.2f); graphics.FillPath(&fill,&path); graphics.DrawPath(&edge,&path);
        draw_centered_text(graphics,info.name,{58,y+14,535,32},24,true);
        draw_centered_text(graphics,info.description,{58,y+56,535,24},15,false,Gdiplus::Color(255,193,210,218));
        draw_centered_text(graphics,info.status,{58,y+91,535,22},14,false,Gdiplus::Color(255,151,216,185));
        plugin_action_button(graphics,info.builtin?L"内置":info.enabled?L"禁用":L"启用",{620,y+40,116,44},info.enabled);
    }
    plugin_action_button(graphics,L"打开插件文件夹",{40,647,225,43});
    plugin_action_button(graphics,L"刷新列表",{287,647,225,43});
    plugin_action_button(graphics,L"开发者指南",{535,647,225,43});
}
void handle_plugin_management(HWND window,const Gdiplus::PointF& point) {
    if (point_in_logical_rect(point,40,647,225,43)) {
        ShellExecuteW(window,L"open",plugin_manager.directory().c_str(),nullptr,nullptr,SW_SHOWNORMAL); return;
    }
    if (point_in_logical_rect(point,287,647,225,43)) { refresh_plugins(main_window); InvalidateRect(window,nullptr,FALSE); return; }
    if (point_in_logical_rect(point,535,647,225,43)) {
        auto file=executable_directory()+L"\\README.md";
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32) {
            MessageBoxW(window,L"请打开项目 README.md 中的开发者指南。",L"开发者指南",MB_OK|MB_ICONINFORMATION);
        } return;
    }
    for (int visible=0;visible<4;++visible) {
        std::size_t index=plugin_scroll+visible;
        if (index>=plugin_manager.plugins().size() || !point_in_logical_rect(point,620,134+visible*136.0f,116,44)) { continue; }
        const auto& item=plugin_manager.plugins()[index];
        if (item.builtin) { return; }
        else {
            bool enabled=!item.enabled;
            if (enabled) {
                auto prompt=L"插件能够执行本机代码，请只启用你信任的扩展。\n\n是否启用「"+item.name+L"」？";
                if (MessageBoxW(window,prompt.c_str(),L"确认启用插件",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) { return; }
            }
            std::wstring error;
            if (plugin_manager.set_enabled(index,enabled,error)) {
                update_plugin_interface(main_window);
            } else { MessageBoxW(window,error.c_str(),L"插件设置失败",MB_OK|MB_ICONWARNING); }
        }
        configuration_cached_tab=-1; InvalidateRect(window,nullptr,FALSE); return;
    }
}
void draw_configuration_contents(HWND window,HDC dc,bool composite=true) {
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

    configuration_skins[configuration_tab].draw(graphics,width,height,0.65f);

    ConfigurationTransform transform=configuration_transform(window);
    Gdiplus::GraphicsState state=graphics.Save();
    graphics.TranslateTransform(transform.offset_x,transform.offset_y);
    graphics.ScaleTransform(transform.scale,transform.scale);

    if (configuration_tab==CONFIG_TAB_RADAR) {


        if (!pentagon_image.draw(graphics,{70,20,660,660})) {
            Gdiplus::Pen grid_pen(Gdiplus::Color(115,181,207,180),1.2f);
            for (int ring=1;ring<=4;++ring) {
                std::array<Gdiplus::PointF,5> grid;
                for (std::size_t i=0;i<grid.size();++i) {
                    grid[i]={RADAR_CENTER.X+(RADAR_VERTICES[i].X-RADAR_CENTER.X)*ring/4.0f,
                        RADAR_CENTER.Y+(RADAR_VERTICES[i].Y-RADAR_CENTER.Y)*ring/4.0f};
                }
                graphics.DrawPolygon(&grid_pen,grid.data(),static_cast<INT>(grid.size()));
            }
            const std::array<const wchar_t*,5> names={{L"共同音保留",L"根音运动",L"调性吸引",L"调性一致性",L"声部运动"}};
            const std::array<Gdiplus::RectF,5> labels={{
                {305,35,186,44},{685,259,110,48},{495,658,184,40},{120,636,190,42},{3,259,110,48}
            }};
            for (std::size_t i=0;i<names.size();++i) {
                draw_centered_text(graphics,names[i],labels[i],20.0f,true,Gdiplus::Color(255,241,238,214));
            }
        }
        Gdiplus::Pen axis_pen(Gdiplus::Color(110,70,80,88),1.5f);
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
    else if (configuration_tab==CONFIG_TAB_PLUGINS) { draw_plugin_management(graphics); }
    else { draw_mode_wheel(graphics,composite,transform.scale); }
    float reveal=composite ? configuration_progress(configuration_switched,200) : 1;
    if (reveal<1) {
        BYTE opacity=static_cast<BYTE>(255*(1-reveal));
        Gdiplus::LinearGradientBrush veil(Gdiplus::Point(0,0),Gdiplus::Point(800,800),
            Gdiplus::Color(opacity,23,29,43),Gdiplus::Color(opacity,56,44,76));
        graphics.FillRectangle(&veil,0,0,800,714);
    }
    const std::array<std::wstring,3> tab_titles={{
        L"调式选择",L"和弦行进评价雷达图",L"插件管理"
    }};
    for (int tab=0;tab<3;++tab) {
        auto bounds=configuration_tab_bounds(tab);
        if (tab==CONFIG_TAB_PLUGINS && plugin_tab_art.draw(graphics,bounds,tab==configuration_tab)) { continue; }
        if (tab!=CONFIG_TAB_PLUGINS) {
        if (button_artworks.draw(graphics,tab==CONFIG_TAB_MODE ? ncnl::ButtonArt::ModeTab : ncnl::ButtonArt::RadarTab,
            bounds,tab==configuration_tab)) { continue; }
        }
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
        if (configuration_interactive_resize && configuration_dc) {
            StretchBlt(dc,0,0,width,height,configuration_dc,0,0,configuration_width,configuration_height,SRCCOPY);
        } else {
            if (!configuration_dc || configuration_width!=width || configuration_height!=height) {
                if (configuration_static_dc) {
                    SelectObject(configuration_static_dc,configuration_static_old); DeleteObject(configuration_static_bitmap);
                    DeleteDC(configuration_static_dc); configuration_static_dc=nullptr;
                }
                if (configuration_dc) {
                    SelectObject(configuration_dc,configuration_old_bitmap);
                    DeleteObject(configuration_bitmap); DeleteDC(configuration_dc);
                }
                configuration_dc=CreateCompatibleDC(dc); configuration_bitmap=CreateCompatibleBitmap(dc,width,height);
                configuration_old_bitmap=SelectObject(configuration_dc,configuration_bitmap);
                configuration_width=width; configuration_height=height;
            }
            if (!configuration_static_dc) {
                configuration_static_dc=CreateCompatibleDC(dc);
                configuration_static_bitmap=CreateCompatibleBitmap(dc,width,height);
                configuration_static_old=SelectObject(configuration_static_dc,configuration_static_bitmap);
                configuration_cached_tab=-1;
            }
            const auto& weights=ncnl::progression_weights().values;
            if (configuration_cached_tab!=configuration_tab || configuration_cached_tonic!=pending_tonic ||
                configuration_cached_mode!=pending_mode || configuration_cached_weights!=weights) {
                draw_configuration_contents(window,configuration_static_dc,false);
                configuration_cached_tab=configuration_tab; configuration_cached_tonic=pending_tonic;
                configuration_cached_mode=pending_mode; configuration_cached_weights=weights;
            }
            BitBlt(configuration_dc,0,0,width,height,configuration_static_dc,0,0,SRCCOPY);
            {
                Gdiplus::Graphics graphics(configuration_dc); auto transform=configuration_transform(window);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                graphics.TranslateTransform(transform.offset_x,transform.offset_y); graphics.ScaleTransform(transform.scale,transform.scale);
                if (configuration_tab==CONFIG_TAB_MODE) { draw_wheel_ring(graphics,transform.scale); }
                float reveal=configuration_progress(configuration_switched,200);
                if (reveal<1) {
                    BYTE alpha=static_cast<BYTE>(255*(1-reveal));
                    Gdiplus::LinearGradientBrush veil(Gdiplus::Point(0,0),Gdiplus::Point(800,800),
                        Gdiplus::Color(alpha,23,29,43),Gdiplus::Color(alpha,56,44,76));
                    graphics.FillRectangle(&veil,0,0,800,714);
                }
            }
            BitBlt(dc,0,0,width,height,configuration_dc,0,0,SRCCOPY);
        }
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
    case WM_TIMER:
        if (w_param==CONFIGURATION_TRANSITION_TIMER) {
            float opening=configuration_progress(configuration_opened,configuration_closing ? 140 : 200);
            int alpha=static_cast<int>(255*(configuration_closing ? 1-opening : opening));
            if (alpha!=configuration_opacity) {
                SetLayeredWindowAttributes(window,0,static_cast<BYTE>(alpha),LWA_ALPHA); configuration_opacity=alpha;
            }
            if (configuration_closing && opening>=1) { DestroyWindow(window); return 0; }
            float rotation=configuration_progress(wheel_started,190);
            double previous_rotation=wheel_rotation;
            if (wheel_started && !wheel_dragging) { wheel_rotation=wheel_from+(wheel_target-wheel_from)*rotation; }
            if (!configuration_interactive_resize && !IsIconic(window) &&
                (!configuration_static_dc || configuration_progress(configuration_switched,200)<1 ||
                 wheel_dragging || previous_rotation!=wheel_rotation)) {
                InvalidateRect(window,nullptr,FALSE);
            }
            if (opening>=1 && configuration_progress(configuration_switched,200)>=1 && rotation>=1 && !wheel_dragging) {
                configuration_animating=false; update_animation_clock();
            }
            return 0;
        }
        break;
    case WM_SIZE:
        if (w_param==SIZE_MINIMIZED) { configuration_mouse_feedback.clear(); update_animation_clock(); }
        else { update_animation_clock(); }
        break;
    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;
    case WM_SETCURSOR:
        if (LOWORD(l_param)==HTCLIENT || (LOWORD(l_param)>=HTTOP && LOWORD(l_param)<=HTBOTTOMRIGHT)) {
            SetCursor(application_cursor(reinterpret_cast<HWND>(w_param),LOWORD(l_param),
                (GetKeyState(VK_RBUTTON)&0x8000)!=0)); return TRUE;
        }
        break;
    case WM_ENTERSIZEMOVE:
        configuration_interactive_resize=true;
        configuration_mouse_feedback.clear(); update_animation_clock();
        return 0;
    case WM_EXITSIZEMOVE:
        configuration_interactive_resize=false;
        update_animation_clock();
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
    case WM_KEYDOWN:
        if (w_param==VK_RETURN && !configuration_closing && (l_param&(1LL<<30))==0) {
            apply_selected_mode(); SendMessageW(window,WM_CLOSE,0,0); return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        Gdiplus::PointF point=configuration_logical_point(window,l_param);
        for (int tab=0;tab<3;++tab) {
            if (configuration_tab_bounds(tab).Contains(point)) {
                configuration_tab=tab; configuration_switched=monotonic_ms();
                animate_configuration(window); return 0;
            }
        }
        if (configuration_tab==CONFIG_TAB_PLUGINS) { handle_plugin_management(window,point); return 0; }
        if (configuration_tab==CONFIG_TAB_MODE) {
            if (point_in_logical_rect(point,230,670,340,40)) {
                apply_selected_mode(); SendMessageW(window,WM_CLOSE,0,0); return 0;
            }
            for (int mode=0;mode<7;++mode) {
                int row=mode/4,column=mode%4;
                if (point_in_logical_rect(point,62.0f+171*column+(row ? 85 : 0),565.0f+49*row,162,40)) {
                    pending_mode=mode; configuration_switched=0; InvalidateRect(window,nullptr,FALSE); return 0;
                }
            }
            float dx=point.X-WHEEL_X,dy=point.Y-WHEEL_Y; float radius=std::sqrt(dx*dx+dy*dy);
            if (radius>=WHEEL_INNER && radius<=WHEEL_RADIUS+5) {
                wheel_dragging=true; wheel_moved=false; wheel_started=0;
                wheel_press_angle=wheel_angle(point); SetCapture(window); animate_configuration(window); return 0;
            }
        }
        if (configuration_tab==CONFIG_TAB_RADAR) {
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
        if (wheel_dragging && GetCapture()==window) {
            auto point=configuration_logical_point(window,l_param);
            double angle=wheel_angle(point),delta=angle-wheel_press_angle;
            if (delta>180) { delta-=360; } if (delta<-180) { delta+=360; }
            if (std::abs(delta)>0.2) { wheel_moved=true; }
            wheel_rotation+=delta; wheel_press_angle=angle; pending_tonic=FIFTHS[wheel_top_index()];
            return 0;
        }
        if (dragged_weight_axis>=0 && GetCapture()==window) {
            update_dragged_weight(window,configuration_logical_point(window,l_param));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (wheel_dragging) {
            int index=wheel_top_index();
            if (!wheel_moved) {
                auto point=configuration_logical_point(window,l_param);
                index=static_cast<int>(std::round((wheel_angle(point)+90-wheel_rotation)/30));
            }
            wheel_dragging=false; if (GetCapture()==window) { ReleaseCapture(); }
            rotate_wheel_to(window,index); return 0;
        }
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
        if (wheel_dragging) { wheel_dragging=false; rotate_wheel_to(window,wheel_top_index()); }
        if (dragged_weight_axis>=0) {
            dragged_weight_axis=-1;
            ncnl::save_progression_config(progression_config_path);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (configuration_tab==CONFIG_TAB_PLUGINS) {
            plugin_scroll=std::max(0,std::min(std::max(0,static_cast<int>(plugin_manager.plugins().size())-4),
                plugin_scroll-GET_WHEEL_DELTA_WPARAM(w_param)/WHEEL_DELTA));
            configuration_cached_tab=-1; InvalidateRect(window,nullptr,FALSE); return 0;
        }
        if (configuration_tab==CONFIG_TAB_MODE) {
            int index=wheel_started && !wheel_dragging ? static_cast<int>(std::round(-wheel_target/30)) : wheel_top_index();
            rotate_wheel_to(window,index+GET_WHEEL_DELTA_WPARAM(w_param)/WHEEL_DELTA);
            return 0;
        }
        break;
    case WM_CLOSE:
        configuration_closing=true; configuration_opened=monotonic_ms();
        configuration_mouse_feedback.clear(); EnableWindow(window,FALSE); animate_configuration(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window,CONFIGURATION_TRANSITION_TIMER); wheel_dragging=false; dragged_weight_axis=-1;
        configuration_animating=false;
        if (configuration_dc) {
            SelectObject(configuration_dc,configuration_old_bitmap); DeleteObject(configuration_bitmap); DeleteDC(configuration_dc);
            configuration_dc=nullptr; configuration_bitmap=nullptr;
        }
        if (configuration_static_dc) {
            SelectObject(configuration_static_dc,configuration_static_old); DeleteObject(configuration_static_bitmap);
            DeleteDC(configuration_static_dc); configuration_static_dc=nullptr;
        }
        for (auto& glyph:wheel_glyphs) { glyph.reset(); } wheel_glyph_scale=0;
        ncnl::save_progression_config(progression_config_path);
        configuration_mouse_feedback.shutdown(); update_animation_clock();
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
    initialize_mode_wheel(); configuration_closing=false; configuration_tab=CONFIG_TAB_MODE;
    configuration_opened=monotonic_ms(); configuration_switched=configuration_opened;
    configuration_opacity=0;
    RECT size={0,0,SETTINGS_DESIGN_SIZE,SETTINGS_DESIGN_SIZE};
    AdjustWindowRectEx(&size,style,FALSE,0);
    POINT position=ncnl::centered_window_position(owner,size.right-size.left,size.bottom-size.top);
    configuration_window=CreateWindowExW(
        WS_EX_LAYERED,L"NoChordNoLifeConfigurationWindow",L"NoChordNoLife 配置",
        style,position.x,position.y,size.right-size.left,size.bottom-size.top,
        owner,nullptr,GetModuleHandleW(nullptr),nullptr
    );
    if (!configuration_window) {
        MessageBoxW(owner,L"无法创建配置窗口。",L"配置",MB_OK|MB_ICONERROR);
        return;
    }
    configuration_mouse_feedback.initialize(configuration_window,L":/assets/cursor");
    SetLayeredWindowAttributes(configuration_window,0,0,LWA_ALPHA);
    animate_configuration(configuration_window);
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
    case WM_SETCURSOR:
        if (LOWORD(l_param)==HTCLIENT || (LOWORD(l_param)>=HTTOP && LOWORD(l_param)<=HTBOTTOMRIGHT)) {
            SetCursor(application_cursor(reinterpret_cast<HWND>(w_param),LOWORD(l_param),
                (GetKeyState(VK_RBUTTON)&0x8000)!=0));
            return TRUE;
        }
        break;

    case WM_CREATE: {
        mouse_feedback.initialize(window,L":/assets/cursor");
        main_window=window;
        SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)|WS_CLIPCHILDREN);
        plugin_container=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,1,1,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        refresh_plugins(window);
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
        mode_label=create_control(
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
            45,152,140,60,window,ID_PRESET_ACTION,FontKind::card
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
        if (plugin_manager.active_index()!=0) { return 0; }
        cancel_header_drag(window,true);
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        int position=piano_header_at_client_point(window,point);
        if (position>=0) {
            begin_chord_edit(window,position);
            return 0;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT plugin_point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        if (switch_plugin_at(window,plugin_point) || plugin_manager.active_index()!=0) { return 0; }
        if (editing_chord>=0) {
            commit_chord_editor(false);
        }
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        int header=piano_header_at_client_point(window,point);
        if (header>=0) {
            begin_header_drag(window,header,point);
            return 0;
        }
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
        if (header_drag_block>=0) {
            if (!(w_param&MK_LBUTTON)) { cancel_header_drag(window,true); }
            else {
                header_drag_pointer={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
                continue_header_drag(window);
            }
            return 0;
        }
        if (middle_dragging && (w_param&MK_MBUTTON)) {
            sweep_rhythm_splits(window,{GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)});
            return 0;
        }
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
        if (header_drag_block>=0) { cancel_header_drag(window,true); return 0; }
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
        cancel_header_drag(window);
        middle_dragging=false;
        middle_visited.clear();
        mouse_feedback.break_trail();
        if (key_preview_gesture) { stop_key_preview(); }
        key_preview_gesture=false;
        if (dragged_midi_position>=0) {
            dragged_midi_position=-1;
            dragged_midi_event=-1;
            dragged_midi_pitch=-1;
        }
        return 0;

    case WM_CLEAR_CHORD_HOVER: {
        if (plugin_manager.active_index()!=0) { return 0; }
        cancel_header_drag(window,true);
        key_preview_gesture=false;
        stop_key_preview();
        POINT point={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        clear_chord_at_client_point(window,point);
        return 0;
    }

    case WM_KILLFOCUS:
    case WM_CANCELMODE:
        cancel_header_drag(window,true);
        key_preview_gesture=false;
        stop_key_preview();
        if (GetCapture()==window) { ReleaseCapture(); }
        break;

    case WM_ACTIVATEAPP:
        if (!w_param) {
            cancel_header_drag(window,true);
            mouse_feedback.clear();
            configuration_mouse_feedback.clear();
            ncnl::preset_library_feedback().clear();
            middle_dragging=false;
            update_animation_clock();
            key_preview_gesture=false;
            stop_key_preview();
        }
        break;

    case WM_MBUTTONDOWN:
        if (plugin_manager.active_index()!=0) { return 0; }
        cancel_header_drag(window,true);
        middle_dragging=true;
        middle_visited.clear();
        middle_previous={GET_X_LPARAM(l_param),GET_Y_LPARAM(l_param)};
        SetCapture(window);
        toggle_rhythm_split(window,middle_previous);
        return 0;

    case WM_MBUTTONUP:
        middle_dragging=false;
        middle_visited.clear();
        if (GetCapture()==window && !(w_param&(MK_LBUTTON|MK_RBUTTON))) { ReleaseCapture(); }
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
            point.y/scale>=ROLL_TOP && point.y/scale<=645 && length>0) {
            import_midi_file(window,path.data());
        }
        return 0;
    }

    case WM_TIMER:
        if (w_param==HEADER_DRAG_TIMER) { continue_header_drag(window); return 0; }
        if (w_param==EMOTION_GIF_TIMER) {
            bool cards_changed=false;
            if (!IsIconic(window) && !interactive_resize) {
                ULONGLONG now=monotonic_ms();
                for (std::size_t i=0;i<emotion_images.size();++i) {
                    if (!advance_emotion_animation(i,now)) { continue; }
                    if (preset_buttons[i]) { InvalidateRect(preset_buttons[i],nullptr,FALSE); }
                    cards_changed=cards_changed || std::find(slot_presets.begin(),slot_presets.end(),
                        static_cast<int>(i))!=slot_presets.end();
                }
                if (cards_changed) {
                    RECT cards={static_cast<LONG>(132*current_scale),static_cast<LONG>(SLOT_Y*current_scale),
                        static_cast<LONG>(951*current_scale),static_cast<LONG>(920*current_scale)};
                    InvalidateRect(window,&cards,FALSE);
                }
            }
            update_emotion_animation_timer();
            return 0;
        }
        if (w_param==KEY_PREVIEW_TIMER) {
            if (!key_preview_gesture) { stop_key_preview(); }
            return 0;
        }
        {
        RECT area=animation_effect_bounds();
        bool redraw_roll=playback_loop_ms>0 || !dissolving_notes.empty();
        ULONGLONG now=monotonic_ms();
        mouse_feedback.expire(now);
        configuration_mouse_feedback.expire(now);
        ncnl::preset_library_feedback().expire(now);
        if (w_param==DISSOLVE_TIMER || w_param==PLAYBACK_TIMER) { expire_dissolving_notes(now); }
        if ((w_param==PLAYBACK_TIMER || w_param==DISSOLVE_TIMER) && !IsIconic(window)) {
            if (configuration_animating && configuration_window) {
                SendMessageW(configuration_window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
                UpdateWindow(configuration_window);
            }
            if (redraw_roll) { InvalidateRect(window,&area,FALSE); }
            mouse_feedback.render(now);
            configuration_mouse_feedback.render(now);
            ncnl::preset_library_feedback().render(now);
        }
        }
        return 0;

    case WM_ANIMATION_FRAME: {
        InterlockedExchange(&animation_frame_pending,0);
        if (configuration_animating && configuration_window && !IsIconic(configuration_window) && !configuration_interactive_resize) {
            SendMessageW(configuration_window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
            if (configuration_window) { UpdateWindow(configuration_window); }
        }
        bool redraw_roll=playback_loop_ms>0 || !dissolving_notes.empty();
        RECT area=animation_effect_bounds();
        ULONGLONG now=monotonic_ms();
        mouse_feedback.expire(now);
        configuration_mouse_feedback.expire(now);
        ncnl::preset_library_feedback().expire(now);
        expire_dissolving_notes(now);
        if (!IsIconic(window) && !interactive_resize) {
            if (redraw_roll) { InvalidateRect(window,&area,FALSE); UpdateWindow(window); }
            mouse_feedback.render(now);
            configuration_mouse_feedback.render(now);
            ncnl::preset_library_feedback().render(now);
        }
        return 0;
    }

    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;

    case WM_ENTERSIZEMOVE:
        mouse_feedback.clear();
        cancel_header_drag(window,true);
        commit_chord_editor(false);
        interactive_resize=true;
        main_background_dirty=true;
        update_animation_clock();
        update_emotion_animation_timer();
        return 0;

    case WM_SIZE:
        if (w_param==SIZE_MINIMIZED) { mouse_feedback.clear(); cancel_header_drag(window,true); }
        expire_dissolving_notes(monotonic_ms());
        update_animation_clock();
        update_emotion_animation_timer(true);
        if (w_param!=SIZE_MINIMIZED && !interactive_resize && !controls.empty()) {
            apply_layout(window);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        interactive_resize=false;
        main_background_dirty=true;
        update_animation_clock();
        update_emotion_animation_timer(true);
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
        HWND control=reinterpret_cast<HWND>(l_param);
        if (control==mode_label && ensure_main_background_buffer(window,dc)) {
            POINT origin={0,0}; MapWindowPoints(control,window,&origin,1);
            RECT bounds={}; GetClientRect(control,&bounds);
            BitBlt(dc,0,0,bounds.right,bounds.bottom,main_background_dc,origin.x,origin.y,SRCCOPY);
        }
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

        if (l_param!=-1) {
            return 0;
        }
        if (slot_presets[active_slot]!=-1) { remember_edit(); }
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
        if (id==ID_PRESET_ACTION && notification==BN_CLICKED) {
            ncnl::open_preset_library(window,executable_directory()+L"\\presents",
                L":/assets/cursor",interface_font_family.get(),interface_font_name,
                [](const std::wstring& path){ return import_midi_file(ncnl::preset_library_window(),path); },
                [](){ return ncnl::encode_midi(rhythm,BPM); },
                L":/assets/skins/preset.png",
                L":/assets/skins/buttons");
            return 0;
        }
        if (id>=ID_PRESET_BASE && id<ID_PRESET_BASE+5 && notification==BN_CLICKED) {
            if (slot_presets[active_slot]!=id-ID_PRESET_BASE) { remember_edit(); }
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

    case WM_KEYDOWN:
        if (header_drag_block>=0) {
            cancel_header_drag(window,true);
            if (w_param==VK_ESCAPE) { return 0; }
        }
        if (w_param=='Z' && (GetKeyState(VK_CONTROL)&0x8000)) {
            undo_last_edit();
            return 0;
        }
        break;

    case WM_DESTROY:
        stop_animation_clock();
        plugin_manager.shutdown(); plugin_container=nullptr; plugin_button_art.clear();
        cancel_header_drag(window);
        if (configuration_window) { DestroyWindow(configuration_window); }
        ncnl::close_preset_library();
        mouse_feedback.shutdown();
        configuration_mouse_feedback.shutdown();
        KillTimer(window,EMOTION_GIF_TIMER);
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

}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show_command) {
    try { initialize_runtime_storage(executable_directory()); }
    catch (const std::exception& error) { MessageBoxW(nullptr,utf8_to_wide(error.what()).c_str(),L"启动失败",MB_OK|MB_ICONERROR); return 1; }
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

    load_interface_font(L":/assets/res/font.ttf");

    load_application_skins(L":/assets");
    load_interface_images();

    const wchar_t CLASS_NAME[]=L"NoChordNoLifeGeneratorWindow";
    WNDCLASSW window_class{};
    window_class.style=CS_DBLCLKS;
    window_class.lpfnWndProc=window_procedure;
    window_class.hInstance=instance;
    window_class.lpszClassName=CLASS_NAME;
    window_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    window_class.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
    window_class.hbrBackground=nullptr;

    if (!RegisterClassW(&window_class)) {
        ncnl_button_art.reset(); plugin_tab_art.reset();
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
    configuration_class.hIcon=window_class.hIcon;
    configuration_class.hbrBackground=nullptr;
    if (!RegisterClassW(&configuration_class)) {
        ncnl_button_art.reset(); plugin_tab_art.reset();
        unload_interface_font();
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    DWORD style=WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX;
    DWORD ex_style=0;
    RECT initial_size={0,0,DESIGN_WIDTH,DESIGN_HEIGHT};
    AdjustWindowRectEx(&initial_size,style,FALSE,ex_style);

    HWND window=CreateWindowExW(
        ex_style,CLASS_NAME,L"NoChordNoLife！",
        style,
        CW_USEDEFAULT,CW_USEDEFAULT,
        initial_size.right-initial_size.left,
        initial_size.bottom-initial_size.top,
        nullptr,nullptr,instance,nullptr
    );
    if (!window) {
        ncnl_button_art.reset(); plugin_tab_art.reset(); plugin_button_art.clear();
        background_image.reset();
        pentagon_image.reset(); button_artworks.reset();
        for (auto& skin:configuration_skins) { skin.reset(); }
        unload_interface_font();
        Gdiplus::GdiplusShutdown(gdiplus_token);
        return 1;
    }

    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));
    ShowWindow(window,show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message,nullptr,0,0)>0) {
        bool first_key_press=(message.lParam&(1LL<<30))==0;
        bool editing=message.hwnd==chord_editor || GetFocus()==chord_editor;
        bool main_window_key=GetAncestor(message.hwnd,GA_ROOT)==main_window && plugin_manager.active_index()==0;
        if (main_window_key || (configuration_window && GetAncestor(message.hwnd,GA_ROOT)==configuration_window) ||
            (ncnl::preset_library_window() && GetAncestor(message.hwnd,GA_ROOT)==ncnl::preset_library_window())) {
            track_mouse_feedback_message(message);
        }
        if (main_window_key && message.hwnd!=main_window &&
            (message.message==WM_MBUTTONDOWN || message.message==WM_MBUTTONUP)) {
            POINT point={GET_X_LPARAM(message.lParam),GET_Y_LPARAM(message.lParam)};
            MapWindowPoints(message.hwnd,main_window,&point,1);
            SendMessageW(main_window,message.message,message.wParam,MAKELPARAM(point.x,point.y));
            continue;
        }
        if (main_window_key && message.message==WM_KEYDOWN && message.wParam=='Z' &&
            (GetKeyState(VK_CONTROL)&0x8000) && !editing) {
            cancel_header_drag(main_window,true);
            if (first_key_press) { undo_last_edit(); }
            continue;
        }
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
            if (GetCapture()==main_window && !middle_dragging) {
                ReleaseCapture();
            }
            continue;
        }
        if (message.message==WM_KEYDOWN && first_key_press && !editing &&
            main_window_key) {
            cancel_header_drag(main_window,true);
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
    pentagon_image.reset(); button_artworks.reset();
    ncnl_button_art.reset(); plugin_tab_art.reset(); plugin_button_art.clear();
    for (auto& skin:configuration_skins) { skin.reset(); }
    arrow_image.reset();
    for (auto& image:emotion_images) {
        image.reset();
    }
    for (auto& animation:emotion_animations) { animation.frames.clear(); }
    if (title_font) {
        DeleteObject(title_font);
    }
    if (normal_font) {
        DeleteObject(normal_font);
    }
    if (card_font) {
        DeleteObject(card_font);
    }
    unload_interface_font();
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return static_cast<int>(message.wParam);
}
