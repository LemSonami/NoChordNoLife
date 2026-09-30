#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <gdiplus.h>

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

namespace {

constexpr int ID_GENERATE=1003;
constexpr int ID_SETTINGS=1004;
constexpr int ID_PRESET_ACTION=1005;
constexpr int ID_PRESET_BASE=1100;
constexpr int ID_SLOT_BASE=1200;

constexpr int DESIGN_WIDTH=1000;
constexpr int DESIGN_HEIGHT=1000;
constexpr int MIN_CLIENT_SIZE=600;
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
HWND settings_button=nullptr;
HWND generate_button=nullptr;
std::array<HWND,5> preset_buttons={{nullptr,nullptr,nullptr,nullptr,nullptr}};
std::array<HWND,4> slot_buttons={{nullptr,nullptr,nullptr,nullptr}};
std::array<HWND,4> preset_labels={{nullptr,nullptr,nullptr,nullptr}};
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

ULONG_PTR gdiplus_token=0;
std::unique_ptr<Gdiplus::Image> background_image;
std::wstring background_path;
std::array<std::unique_ptr<Gdiplus::Image>,5> emotion_images;
std::unique_ptr<Gdiplus::Image> arrow_image;
std::string current_mode="C Ionian";
ncnl::GeneratedProgression displayed_progression{};
bool has_displayed_progression=false;

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

void generate_and_show(HWND owner) {
    try {
        std::array<ncnl::ChordConstraint,4> constraints;
        for (int position=0;position<4;++position) {
            constraints[position].emotion_preset=slot_presets[position];
            constraints[position].fixed_notes="";
        }

        displayed_progression=ncnl::generate_progression(
            current_mode,constraints
        );
        has_displayed_progression=true;
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

void draw_piano_roll(
    Gdiplus::Graphics& graphics,
    float offset_x,
    float offset_y,
    float scale
) {
    const float left=offset_x+50.0f*scale;
    const float top=offset_y+295.0f*scale;
    const float roll_width=900.0f*scale;
    const float roll_height=350.0f*scale;
    const float header_height=52.0f*scale;
    const float grid_top=top+header_height;
    const float grid_height=roll_height-header_height;
    const float section_width=roll_width/4.0f;
    const float row_height=grid_height/24.0f;

    Gdiplus::SolidBrush frame(Gdiplus::Color(238,22,27,34));
    Gdiplus::SolidBrush header(Gdiplus::Color(245,35,42,53));
    Gdiplus::Pen outline(Gdiplus::Color(255,165,180,205),2.0f*scale);
    graphics.FillRectangle(&frame,left,top,roll_width,roll_height);
    graphics.FillRectangle(&header,left,top,roll_width,header_height);

    for (int lane=0;lane<24;++lane) {
        int midi=71-lane;
        int pitch_class=midi%12;
        bool black_key=pitch_class==1 || pitch_class==3 || pitch_class==6 ||
                       pitch_class==8 || pitch_class==10;
        Gdiplus::SolidBrush lane_brush(
            black_key ? Gdiplus::Color(235,27,31,38)
                      : Gdiplus::Color(225,42,47,56)
        );
        float y=grid_top+lane*row_height;
        graphics.FillRectangle(&lane_brush,left,y,roll_width,row_height);
    }

    Gdiplus::Pen row_line(Gdiplus::Color(100,92,103,118),1.0f);
    for (int lane=0;lane<=24;++lane) {
        float y=grid_top+lane*row_height;
        graphics.DrawLine(&row_line,left,y,left+roll_width,y);
    }

    Gdiplus::Pen section_line(Gdiplus::Color(230,150,166,190),2.0f*scale);
    for (int position=0;position<=4;++position) {
        float x=left+position*section_width;
        graphics.DrawLine(&section_line,x,top,x,top+roll_height);
    }

    if (!has_displayed_progression) {
        draw_centered_text(
            graphics,L"点击“生成”后，四个和弦将在钢琴卷帘中显示",
            Gdiplus::RectF(left,grid_top,roll_width,grid_height),
            20.0f*scale,false,Gdiplus::Color(210,210,220,235)
        );
    }
    else {
        Gdiplus::SolidBrush note_fill(Gdiplus::Color(255,55,206,235));
        Gdiplus::Pen note_edge(Gdiplus::Color(255,166,244,255),1.5f*scale);
        for (int position=0;position<4;++position) {
            const auto& chord=displayed_progression.chords[position];
            std::wostringstream caption;
            caption<<utf8_to_wide(chord.notes)<<L"   "
                   <<std::fixed<<std::setprecision(1)<<chord.emotion_score;
            draw_centered_text(
                graphics,caption.str(),
                Gdiplus::RectF(
                    left+position*section_width,top,section_width,header_height
                ),
                16.0f*scale,true
            );

            ncnl::Chord notes=ncnl::parse_chord(chord.notes);
            for (int pitch_class:notes) {
                int midi=60+pitch_class;
                int lane=71-midi;
                float note_x=left+position*section_width+12.0f*scale;
                float note_y=grid_top+lane*row_height+1.5f*scale;
                float note_width=section_width-24.0f*scale;
                float note_height=std::max(2.0f,row_height-3.0f*scale);
                graphics.FillRectangle(
                    &note_fill,note_x,note_y,note_width,note_height
                );
                graphics.DrawRectangle(
                    &note_edge,note_x,note_y,note_width,note_height
                );
            }
        }
    }
    graphics.DrawRectangle(&outline,left,top,roll_width,roll_height);

    if (has_displayed_progression) {
        std::wostringstream quality;
        quality<<L"进行质量 "<<std::fixed<<std::setprecision(1)
               <<displayed_progression.quality_score;
        draw_centered_text(
            graphics,quality.str(),
            Gdiplus::RectF(
                offset_x+360.0f*scale,offset_y+655.0f*scale,
                280.0f*scale,35.0f*scale
            ),
            15.0f*scale,true
        );
    }

    for (int transition=0;transition<3;++transition) {
        float x=offset_x+(250.0f+225.0f*transition)*scale;
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

void paint_window(HWND window) {
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
        draw_background(window,buffer,!interactive_resize);
        BitBlt(dc,0,0,width,height,buffer,0,0,SRCCOPY);
        SelectObject(buffer,old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
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

    COLORREF fill=pressed ? RGB(55,91,128) : RGB(67,112,158);
    COLORREF border=RGB(190,210,235);
    int border_width=std::max(1,static_cast<int>(std::lround(2*current_scale)));
    Gdiplus::Image* button_image=nullptr;

    if (id>=ID_PRESET_BASE && id<ID_PRESET_BASE+5) {
        int preset=id-ID_PRESET_BASE;
        fill=preset_color(preset,pressed);
        button_image=emotion_images[static_cast<std::size_t>(preset)].get();
        if (slot_presets[active_slot]==preset) {
            border=RGB(255,255,255);
            border_width=std::max(2,static_cast<int>(std::lround(4*current_scale)));
        }
    }
    else if (id>=ID_SLOT_BASE && id<ID_SLOT_BASE+4) {
        int position=id-ID_SLOT_BASE;
        fill=position==active_slot ? RGB(54,112,171) : RGB(50,58,77);
        int preset=slot_presets[static_cast<std::size_t>(position)];
        if (preset>=0 && preset<5) {
            button_image=emotion_images[static_cast<std::size_t>(preset)].get();
        }
        border=position==active_slot ? RGB(245,214,118) : RGB(160,170,190);
        border_width=position==active_slot
            ? std::max(2,static_cast<int>(std::lround(4*current_scale)))
            : std::max(1,static_cast<int>(std::lround(2*current_scale)));
    }
    else if (id==ID_SETTINGS) {
        fill=pressed ? RGB(80,69,112) : RGB(105,88,145);
    }
    else if (id==ID_GENERATE) {
        fill=pressed ? RGB(44,116,87) : RGB(55,151,111);
        border=RGB(190,244,216);
    }

    HBRUSH fill_brush=CreateSolidBrush(fill);
    HPEN border_pen=CreatePen(PS_SOLID,border_width,border);
    HGDIOBJ old_brush=SelectObject(item->hDC,fill_brush);
    HGDIOBJ old_pen=SelectObject(item->hDC,border_pen);
    Rectangle(item->hDC,bounds.left,bounds.top,bounds.right,bounds.bottom);
    SelectObject(item->hDC,old_pen);
    SelectObject(item->hDC,old_brush);
    DeleteObject(border_pen);
    DeleteObject(fill_brush);

    if (button_image) {
        Gdiplus::Graphics graphics(item->hDC);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        int inset=std::max(border_width,1);
        int image_width=std::max(
            1,static_cast<int>(bounds.right-bounds.left)-2*inset
        );
        int image_height=std::max(
            1,static_cast<int>(bounds.bottom-bounds.top)-2*inset
        );
        graphics.DrawImage(
            button_image,
            Gdiplus::Rect(
                bounds.left+inset,bounds.top+inset,
                image_width,image_height
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
    }

    if (button_image) {
        HPEN image_border=CreatePen(PS_SOLID,border_width,border);
        HGDIOBJ old_image_pen=SelectObject(item->hDC,image_border);
        HGDIOBJ old_image_brush=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));
        Rectangle(item->hDC,bounds.left,bounds.top,bounds.right,bounds.bottom);
        SelectObject(item->hDC,old_image_brush);
        SelectObject(item->hDC,old_image_pen);
        DeleteObject(image_border);
        return;
    }

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

LRESULT CALLBACK window_procedure(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    switch (message) {
    case WM_CREATE: {
        main_window=window;
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

        create_control(
            L"STATIC",L"拖动上方情感图片到下方和弦方框；右键方框可清空",
            WS_CHILD|WS_VISIBLE|SS_CENTER,
            180,252,640,28,window,0,FontKind::hint
        );

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

    case WM_SIZING:
        enforce_square_resize(window,w_param,reinterpret_cast<RECT*>(l_param));
        return TRUE;

    case WM_ENTERSIZEMOVE:
        interactive_resize=true;
        return 0;

    case WM_SIZE:
        if (w_param!=SIZE_MINIMIZED && !interactive_resize && !controls.empty()) {
            apply_layout(window);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        interactive_resize=false;
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
            choose_background(window);
            return 0;
        }
        break;
    }

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

    Gdiplus::GdiplusStartupInput gdiplus_input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&gdiplus_input,nullptr)!=Gdiplus::Ok) {
        MessageBoxW(nullptr,L"无法初始化图片组件。",L"启动失败",MB_OK|MB_ICONERROR);
        return 1;
    }

    background_path=executable_directory()+L"\\assets\\bg.png";
    load_background(background_path);
    load_interface_images();

    const wchar_t CLASS_NAME[]=L"NoChordNoLifeGeneratorWindow";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc=window_procedure;
    window_class.hInstance=instance;
    window_class.lpszClassName=CLASS_NAME;
    window_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    window_class.hbrBackground=nullptr;

    if (!RegisterClassW(&window_class)) {
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    background_image.reset();
    arrow_image.reset();
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
