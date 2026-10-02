#include "../chord_gui.cpp"
#include <iostream>

void require(bool ok,const char* message) { if (!ok) { throw std::runtime_error(message); } }
namespace ncnl {
struct MouseFeedbackTestAccess {
    static void preview(MouseFeedback& feedback) {
        require(feedback.pixels && feedback.buffer,"overlay has no rendered pixels");
        int green=0,grey=0,red=0,visible=0;
        auto data=static_cast<DWORD*>(feedback.pixels);
        for (int i=0;i<feedback.buffer_width*feedback.buffer_height;++i) {
            DWORD pixel=data[i]; int a=(pixel>>24)&255,r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
            if (a<25) { continue; } ++visible;
            require(r<=a && g<=a && b<=a,"overlay RGB is not premultiplied");
            green+=g>r+12; red+=r>g+12; grey+=std::abs(r-g)<8 && std::abs(g-b)<8;
        }
        require(visible>50 && green>10 && grey>10 && red>10,"three trail colors are not visible");
        Gdiplus::Bitmap source(feedback.buffer_width,feedback.buffer_height,feedback.buffer_width*4,
            PixelFormat32bppPARGB,static_cast<BYTE*>(feedback.pixels));
        Gdiplus::Bitmap snapshot(feedback.buffer_width,feedback.buffer_height,PixelFormat32bppARGB);
        { Gdiplus::Graphics graphics(&snapshot); graphics.Clear(Gdiplus::Color(255,25,29,44));
            graphics.DrawImage(&source,0,0); }
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        require(snapshot.Save(L"tests\\mouse_feedback_preview.png",&png,nullptr)==Gdiplus::Ok,"preview save failed");
    }
};
}
int main() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) { return 1; }
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure; type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"MouseFeedbackRegression"; RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"Mouse feedback test",WS_OVERLAPPEDWINDOW,
        20,20,size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    int result=0;
    try {
        require(window!=nullptr,"test window creation failed");
        mouse_feedback.initialize(window,L"assets\\cursor");
        ncnl::AppCursor types[]={ncnl::AppCursor::link,ncnl::AppCursor::vertical,
            ncnl::AppCursor::unavailable,ncnl::AppCursor::text};
        LPCWSTR defaults[]={IDC_HAND,IDC_SIZENS,IDC_NO,IDC_IBEAM};
        for (int i=0;i<4;++i) {
            auto cursor=mouse_feedback.cursor(types[i]);
            require(cursor && cursor!=LoadCursorW(nullptr,defaults[i]),"ANI asset did not load");
            ICONINFO info{}; require(GetIconInfo(cursor,&info)!=FALSE,"cursor invalid");
            if (info.hbmColor) { DeleteObject(info.hbmColor); } if (info.hbmMask) { DeleteObject(info.hbmMask); }
        }
        require(application_cursor(window,HTCLIENT,false)==mouse_feedback.cursor(ncnl::AppCursor::link),"default cursor mismatch");
        require(application_cursor(window,HTTOP,false)==mouse_feedback.cursor(ncnl::AppCursor::vertical),"resize cursor mismatch");
        require(application_cursor(chord_editor,HTCLIENT,false)==mouse_feedback.cursor(ncnl::AppCursor::text),"text cursor mismatch");
        require(application_cursor(window,HTCLIENT,true)==mouse_feedback.cursor(ncnl::AppCursor::unavailable),"right cursor mismatch");
        rhythm.events={{0,0.5,90,{}},{1,0.5,90,{}},{2,0.5,90,{}},{3,0.5,90,{}},{4,0.5,90,{}}};
        rhythm.length=5; rhythm_splits.assign(4,false); chord_blocks={{0,5}};
        displayed_progression.chords={{"C E G",50}}; slot_presets={2}; active_slot=0; apply_block_notes(0);
        POINT first={static_cast<LONG>(timeline_x(event_boundary(1))),500};
        POINT last={static_cast<LONG>(timeline_x(event_boundary(4))),500};
        SendMessageW(window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(first.x,first.y));
        SendMessageW(window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(last.x,last.y));
        require(chord_blocks.size()==5,"middle sweep skipped boundaries");
        SendMessageW(window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(first.x,first.y));
        require(chord_blocks.size()==5,"middle sweep toggled visited boundaries");
        SendMessageW(window,WM_MBUTTONUP,0,MAKELPARAM(first.x,first.y));
        require(!middle_dragging && GetCapture()!=window,"middle release retained capture");
        SendMessageW(window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(first.x,first.y));
        SendMessageW(window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(last.x,last.y));
        require(chord_blocks.size()==4,"middle click merge behavior changed");
        SendMessageW(window,WM_MBUTTONUP,0,0);
        ULONGLONG now=monotonic_ms();
        mouse_feedback.input({180,210},MK_LBUTTON,true,1,now);
        mouse_feedback.input({280,220},MK_LBUTTON,false,1,now);
        mouse_feedback.input({340,250},MK_LBUTTON,false,1,now);
        mouse_feedback.input({340,250},0,false,1,now);
        mouse_feedback.input({180,290},MK_MBUTTON,false,1,now);
        mouse_feedback.input({310,310},MK_MBUTTON,false,1,now);
        mouse_feedback.input({350,295},MK_MBUTTON,false,1,now);
        mouse_feedback.input({350,295},0,false,1,now);
        mouse_feedback.input({180,365},MK_RBUTTON,false,1,now);
        mouse_feedback.input({300,350},MK_RBUTTON,false,1,now);
        mouse_feedback.input({360,380},MK_RBUTTON,false,1,now);
        require(mouse_feedback.active() && mouse_feedback.particle_count()>=36,"click/trail particles missing");
        ShowWindow(window,SW_SHOWNOACTIVATE);
        HWND foreground=GetForegroundWindow(); mouse_feedback.render(now+120);
        HWND overlay=mouse_feedback.overlay_window();
        require(overlay!=nullptr && IsWindowVisible(overlay),"overlay not visible");
        LONG_PTR flags=GetWindowLongPtrW(overlay,GWL_EXSTYLE);
        require((flags&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==
            (WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"overlay intercepts clicks or activation");
        require(GetForegroundWindow()==foreground && SendMessageW(overlay,WM_NCHITTEST,0,0)==HTTRANSPARENT,
            "overlay took focus or blocked hit testing");
        ncnl::MouseFeedbackTestAccess::preview(mouse_feedback);
        ULONGLONG start=monotonic_ms();
        for (int i=0;i<120;++i) { mouse_feedback.render(now+120); }
        std::cout<<"Mouse overlay render: "<<(monotonic_ms()-start)/120.0<<" ms/frame\n";
        mouse_feedback.expire(now+1000);
        require(!mouse_feedback.active() && !IsWindowVisible(overlay),"mouse effects did not expire");
        mouse_feedback.clear();
        mouse_feedback.input({400,300},MK_LBUTTON,false,1,now);
        for (int x=401;x<=410;++x) { mouse_feedback.input({x,300},MK_LBUTTON,false,1,now); }
        require(mouse_feedback.trail_count()>1,"slow mouse movement never emits a trail");
        for (int i=0;i<200;++i) { mouse_feedback.input({200+i*2,450},MK_LBUTTON|MK_MBUTTON|MK_RBUTTON,true,1,now); }
        require(mouse_feedback.particle_count()<=512 && mouse_feedback.trail_count()<=192,"mouse effect budgets exceeded");
        mouse_feedback.clear(); update_animation_clock();
        require(!animation_clock && !mouse_feedback.active(),"idle mouse clock leaked");
        std::cout<<"PASS: copied ANI loading/cursor mapping, continuous middle split/no retoggle, burst/three colors, alpha/input passthrough, caps/expiry/cleanup\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    mouse_feedback.clear(); stop_animation_clock();
    if (window) { DestroyWindow(window); }
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    Gdiplus::GdiplusShutdown(gdiplus_token); return result;
}
