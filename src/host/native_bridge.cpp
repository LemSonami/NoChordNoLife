#include "bridge_runtime.hpp"
#include "state.hpp"
#include <mutex>
#include "../ui/chord_gui.cpp"

namespace ncnlplug {
Callbacks host_callbacks{};
bool host_preview=false;
}

namespace {
constexpr UINT_PTR HOST_TIMER=0x4e43;
constexpr UINT RESTORE_STATE=WM_APP+150;
constexpr UINT DESTROY_BRIDGE=WM_APP+151;
HHOOK message_hook=nullptr;
DWORD gui_thread=0;
bool initialized=false,publishing=false,reported_error=false;
std::vector<unsigned char> last_published;
void cleanup();

ncnlplug::State capture() {
    ncnlplug::State s;
    s.rhythm=rhythm; s.splits=rhythm_splits; s.chords.clear();
    for(const auto& c:displayed_progression.chords) s.chords.push_back(c.notes);
    s.presets=slot_presets; s.mode=current_mode; s.file=wide_to_utf8(rhythm_file_name);
    s.weights=ncnl::progression_weights().values;
    s.low=roll_low_pitch; s.high=roll_high_pitch; s.active=active_slot; return s;
}
void publish() {
    if(!main_window || publishing) return;
    publishing=true;
    try {
        auto bytes=ncnlplug::encode(capture());
        if(bytes!=last_published) {
            last_published=bytes;
            auto& c=ncnlplug::host_callbacks;
            if(c.state) c.state(c.context,bytes.data(),static_cast<std::uint32_t>(bytes.size()));
        }
        reported_error=false;
    } catch(const std::exception& error) {
        if(!reported_error) { reported_error=true; MessageBoxW(main_window,utf8_to_wide(error.what()).c_str(),L"插件状态提示",MB_OK|MB_ICONWARNING); }
    }
    publishing=false;
}
void apply(const ncnlplug::State& s) {
    ncnl::parse_mode(s.mode);
    for(const auto& text:s.chords) ncnl::parse_roll_notes(text);
    hide_chord_editor(); discard_note_effects();
    rhythm=s.rhythm; rhythm_splits=s.splits; chord_blocks.clear();
    std::size_t first=0;
    for(std::size_t i=0;i<rhythm_splits.size();++i) if(rhythm_splits[i]) {
        chord_blocks.push_back({first,i+1}); first=i+1;
    }
    chord_blocks.push_back({first,rhythm.events.size()});
    slot_presets=s.presets; displayed_progression.chords.clear();
    current_mode=s.mode;
    for(const auto& text:s.chords) displayed_progression.chords.push_back({text,
        ncnl::roll_emotion_score(current_mode,text)});
    rhythm_file_name=utf8_to_wide(s.file); roll_low_pitch=s.low; roll_high_pitch=s.high; active_slot=s.active;
    ncnl::set_progression_weights({s.weights}); undo_history.clear();
    SetWindowTextW(mode_label,(L"当前调式："+utf8_to_wide(current_mode)).c_str());
    configuration_cached_tab=-1; update_slot_ui(); refresh_progression_display();
    last_published=ncnlplug::encode(s);
}
void filter_message(MSG& message) {
    HWND window=main_window;
        bool first_key_press=(message.lParam&(1LL<<30))==0;
        bool editing=message.hwnd==chord_editor || GetFocus()==chord_editor;
        bool main_window_key=(message.hwnd==main_window || IsChild(main_window,message.hwnd)) && plugin_manager.active_index()==0;
        if (main_window_key || (configuration_window && GetAncestor(message.hwnd,GA_ROOT)==configuration_window) ||
            (ncnl::preset_library_window() && GetAncestor(message.hwnd,GA_ROOT)==ncnl::preset_library_window())) {
            track_mouse_feedback_message(message);
        }
        if (main_window_key && message.hwnd!=main_window &&
            (message.message==WM_MBUTTONDOWN || message.message==WM_MBUTTONUP)) {
            POINT point={GET_X_LPARAM(message.lParam),GET_Y_LPARAM(message.lParam)};
            MapWindowPoints(message.hwnd,main_window,&point,1);
            SendMessageW(main_window,message.message,message.wParam,MAKELPARAM(point.x,point.y));
            message.message=WM_NULL; return;
        }
        if (main_window_key && message.message==WM_KEYDOWN && message.wParam=='Z' &&
            (GetKeyState(VK_CONTROL)&0x8000) && !editing) {
            cancel_header_drag(main_window,true);
            if (first_key_press) { undo_last_edit(); }
            message.message=WM_NULL; return;
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
            message.message=WM_NULL; return;
        }
        if (main_window_key && message.message==WM_RBUTTONUP) {
            if (GetCapture()==main_window && !middle_dragging) {
                ReleaseCapture();
            }
            message.message=WM_NULL; return;
        }
        if (message.message==WM_KEYDOWN && first_key_press && !editing &&
            main_window_key) {
            cancel_header_drag(main_window,true);
            if (message.wParam==VK_SPACE) {
                toggle_midi_playback(window);
                message.message=WM_NULL; return;
            }
            if (message.wParam==VK_RETURN) {
                SendMessageW(
                    window,WM_COMMAND,MAKEWPARAM(ID_GENERATE,BN_CLICKED),
                    reinterpret_cast<LPARAM>(generate_button)
                );
                message.message=WM_NULL; return;
            }
        }
}
LRESULT CALLBACK hook(int code,WPARAM w,LPARAM l) {
    if(code>=0 && w==PM_REMOVE) {
        MSG& m=*reinterpret_cast<MSG*>(l);
        filter_message(m);
    }
    return CallNextHookEx(message_hook,code,w,l);
}
LRESULT CALLBACK bridge_procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==DESTROY_BRIDGE) { cleanup(); return 0; }
    if(message==WM_PRINTCLIENT) {
        draw_background(window,reinterpret_cast<HDC>(w),true);
        return 0;
    }
    if(message==RESTORE_STATE) {
        std::unique_ptr<ncnlplug::State> state(reinterpret_cast<ncnlplug::State*>(l));
        try { apply(*state); } catch(...) {}
        return 0;
    }
    if(message==WM_TIMER && w==HOST_TIMER) {
        auto& c=ncnlplug::host_callbacks;
        if(c.telemetry) {
            auto t=c.telemetry(c.context);
            BPM=static_cast<int>(std::round(t.bpm));
            if(t.playing) {
                playback_loop_ms=rhythm.length*60000.0/t.bpm;
                double phase=std::fmod(std::max(0.0,t.beat),rhythm.length);
                playback_start_ms=monotonic_ms()-static_cast<ULONGLONG>(phase*60000.0/t.bpm);
            } else playback_loop_ms=0;
            update_animation_clock();
        }
        publish(); return 0;
    }
    auto result=window_procedure(window,message,w,l);
    if(message==WM_COMMAND || message==WM_LBUTTONUP || message==WM_RBUTTONUP ||
        message==WM_MBUTTONUP || message==WM_DROPFILES || message==WM_KEYUP) publish();
    return result;
}
void cleanup() {
    if(main_window && gui_thread && GetCurrentThreadId()!=gui_thread) {
        SendMessageW(main_window,DESTROY_BRIDGE,0,0); return;
    }
    if(message_hook) { UnhookWindowsHookEx(message_hook); message_hook=nullptr; }
    if(main_window) {
        MSG pending{};
        while(PeekMessageW(&pending,main_window,RESTORE_STATE,RESTORE_STATE,PM_REMOVE))
            delete reinterpret_cast<ncnlplug::State*>(pending.lParam);
        KillTimer(main_window,HOST_TIMER); DestroyWindow(main_window);
    }
    controls.clear();
    ncnl::close_preset_library();
    background_image.reset(); pentagon_image.reset(); button_artworks.reset();
    ncnl_button_art.reset(); plugin_tab_art.reset(); plugin_button_art.clear();
    for(auto& s:configuration_skins) s.reset();
    arrow_image.reset(); for(auto& i:emotion_images) i.reset();
    for(auto& a:emotion_animations) a.frames.clear();
    wheel_glyphs={};
    for(auto font:{title_font,normal_font,card_font}) if(font) DeleteObject(font);
    title_font=normal_font=card_font=nullptr;
    unload_interface_font();
    for(const auto* name:{L"NoChordNoLifeGeneratorWindow",L"NoChordNoLifeConfigurationWindow",
        L"NoChordNoLifePresetLibrary",L"NoChordNoLifeMouseEffects"}) UnregisterClassW(name,ncnl::runtime_module());
    if(gdiplus_token) { Gdiplus::GdiplusShutdown(gdiplus_token); gdiplus_token=0; }
    ncnlplug::host_callbacks={}; initialized=false;
}
HWND create(HWND parent,const wchar_t* directory,const ncnlplug::Callbacks* callbacks,
    const unsigned char* state,std::uint32_t size) {
    try {
        if(initialized && !main_window) cleanup();
        ncnlplug::host_callbacks=*callbacks;
        if(main_window) {
            SetParent(main_window,parent); ShowWindow(main_window,SW_SHOW); SetTimer(main_window,HOST_TIMER,33,nullptr); return main_window;
        }
        gui_thread=GetCurrentThreadId(); ncnl::runtime_directory()=directory;
        initialize_runtime_storage(directory);
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) throw std::runtime_error("无法初始化插件图片组件。");
        initialized=true;
        load_interface_font(L":/assets/res/font.ttf"); load_application_skins(L":/assets"); load_interface_images();
        WNDCLASSW cls{}; cls.style=CS_DBLCLKS; cls.hInstance=ncnl::runtime_module();
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(1));
        cls.lpszClassName=L"NoChordNoLifeGeneratorWindow"; cls.lpfnWndProc=bridge_procedure;
        if(!RegisterClassW(&cls)) throw std::runtime_error("无法注册插件编辑窗口。");
        cls.lpszClassName=L"NoChordNoLifeConfigurationWindow"; cls.lpfnWndProc=configuration_window_procedure;
        if(!RegisterClassW(&cls)) throw std::runtime_error("无法注册插件配置窗口。");
        auto window=CreateWindowExW(0,L"NoChordNoLifeGeneratorWindow",L"NoChordNoLife",
            WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1000,1000,parent,nullptr,ncnl::runtime_module(),nullptr);
        if(!window) throw std::runtime_error("无法创建插件编辑界面。");
        if(state && size) apply(ncnlplug::decode(state,size));
        message_hook=SetWindowsHookExW(WH_GETMESSAGE,hook,nullptr,gui_thread);
        if(!message_hook) throw std::runtime_error("无法初始化插件快捷键。");
        SetTimer(window,HOST_TIMER,33,nullptr); publish(); return window;
    } catch(const std::exception& error) {
        MessageBoxW(parent,utf8_to_wide(error.what()).c_str(),L"插件界面启动失败",MB_OK|MB_ICONERROR);
        cleanup(); return nullptr;
    }
}
void resize(int width,int height) {
    if(main_window) MoveWindow(main_window,0,0,width,height,TRUE);
}
void detach() {
    publish();
    if(!main_window) return;
    commit_chord_editor(false);
    if(configuration_window) DestroyWindow(configuration_window);
    ncnl::close_preset_library(); mouse_feedback.clear();
    ShowWindow(main_window,SW_HIDE); SetParent(main_window,nullptr);
    update_animation_clock(); update_emotion_animation_timer(true);
}
bool restore(const unsigned char* bytes,std::uint32_t size) {
    try {
        auto s=std::make_unique<ncnlplug::State>(ncnlplug::decode(bytes,size));
        ncnl::parse_mode(s->mode); for(const auto& text:s->chords) ncnl::parse_roll_notes(text);
        if(GetCurrentThreadId()==gui_thread) { apply(*s); return true; }
        if(!main_window || !PostMessageW(main_window,RESTORE_STATE,0,reinterpret_cast<LPARAM>(s.get()))) return false;
        s.release(); return true;
    } catch(...) { return false; }
}
void flush() { if(GetCurrentThreadId()==gui_thread) publish(); }
}

extern "C" __declspec(dllexport) const ncnlplug::BridgeApi* ncnl_bridge_api() {
    static const ncnlplug::BridgeApi api={1,create,resize,detach,cleanup,restore,flush}; return &api;
}
