#include "../chord_gui.cpp"
#include <cassert>
#include <iostream>

int main() {
    Gdiplus::GdiplusStartupInput input;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)==Gdiplus::Ok);
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure; type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"ConfigurationModeRegression"; RegisterClassW(&type);
    RECT frame={0,0,1000,1000}; AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);
    HWND main=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,type.hInstance,nullptr); assert(main);
    WNDCLASSW config{}; config.lpfnWndProc=configuration_window_procedure; config.hInstance=type.hInstance;
    config.lpszClassName=L"NoChordNoLifeConfigurationWindow"; RegisterClassW(&config);
    std::ostringstream fixture; fixture<<"tests/configuration_mode_tmp_"<<GetCurrentProcessId()<<".json";
    progression_config_path=fixture.str(); assert(GetFileAttributesA(progression_config_path.c_str())==INVALID_FILE_ATTRIBUTES);
    for (int tonic=0;tonic<12;++tonic) { for (int mode=0;mode<7;++mode) {
        current_mode=std::string(TONIC_NAMES[tonic])+" "+MODE_NAMES[mode]; initialize_mode_wheel();
        assert(pending_tonic==tonic && pending_mode==mode && FIFTHS[wheel_top_index()]==tonic);
        auto parsed=ncnl::parse_mode(pending_mode_text()); assert(parsed.intervals.size()==7);
    } }
    current_mode="C Ionian";
    for (std::size_t i=0;i<4;++i) { displayed_progression.chords[i]={"C E G",50}; apply_block_notes(i); }
    auto original=rhythm;
    open_configuration_window(main); HWND window=configuration_window; assert(window); ShowWindow(window,SW_HIDE);
    RECT owner_rect{},popup_rect{}; GetWindowRect(main,&owner_rect); GetWindowRect(window,&popup_rect);
    assert(std::abs((owner_rect.left+owner_rect.right)-(popup_rect.left+popup_rect.right))<=2);
    assert(std::abs((owner_rect.top+owner_rect.bottom)-(popup_rect.top+popup_rect.bottom))<=2);
    auto click=[&](int x,int y) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    click(640,750); assert(configuration_tab==2);
    click(584,179); assert(pending_tonic==2 && current_mode=="C Ionian");
    wheel_started=monotonic_ms()-250; configuration_opened=monotonic_ms()-300; configuration_switched=monotonic_ms()-300;
    SendMessageW(window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
    assert(FIFTHS[wheel_top_index()]==2 && std::abs(wheel_rotation+60)<0.001);
    click(310,585); assert(pending_mode==1 && pending_mode_text()=="D Dorian");
    assert(configuration_switched==0); // Mode selection is immediate, without a new fade.
    for (int i=0;i<3;++i) { SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0); }
    assert(pending_tonic==11); // D -> A -> E -> B, even before animation frames arrive.
    rotate_wheel_to(window,0); wheel_started=monotonic_ms()-250;
    SendMessageW(window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(400,73));
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(612,285));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(612,285));
    assert(!wheel_dragging && pending_tonic==3 && GetCapture()!=window);
    rotate_wheel_to(window,2); wheel_started=monotonic_ms()-250;
    SendMessageW(window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
    configuration_switched=monotonic_ms()-300;
    HDC screen=GetDC(nullptr); HDC dc=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,800,800);
    HGDIOBJ old=SelectObject(dc,bitmap); draw_configuration_contents(window,dc);
    LARGE_INTEGER bench_start{},bench_end{},frequency{}; QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&bench_start);
    for (int i=0;i<40;++i) { wheel_rotation+=0.5; draw_configuration_contents(window,dc); }
    QueryPerformanceCounter(&bench_end);
    std::cout<<"Full wheel scene: "<<(bench_end.QuadPart-bench_start.QuadPart)*1000.0/frequency.QuadPart/40<<" ms/frame\n";
    wheel_rotation=-60; draw_configuration_contents(window,dc);
    for (int style=1;style<3;++style) {
        auto& glyph=wheel_glyphs[style]; int opaque=0;
        for (UINT y=0;y<glyph->GetHeight();++y) { for (UINT x=0;x<glyph->GetWidth();++x) {
            Gdiplus::Color pixel; glyph->GetPixel(x,y,&pixel);
            if (pixel.GetA()>128) { ++opaque; assert(pixel.GetR()==0 && pixel.GetG()==0 && pixel.GetB()==0); }
        } }
        assert(opaque>10);
    }
    SendMessageW(window,WM_PAINT,0,0); assert(configuration_static_dc && configuration_dc);
    QueryPerformanceCounter(&bench_start);
    for (int i=0;i<40;++i) {
        wheel_rotation+=0.5; pending_tonic=FIFTHS[wheel_top_index()];
        InvalidateRect(window,nullptr,FALSE); SendMessageW(window,WM_PAINT,0,0);
    }
    QueryPerformanceCounter(&bench_end);
    std::cout<<"Cached configuration paint: "<<(bench_end.QuadPart-bench_start.QuadPart)*1000.0/frequency.QuadPart/40<<" ms/frame\n";
    pending_tonic=2; wheel_rotation=-60; draw_configuration_contents(window,dc);
    {
        Gdiplus::Bitmap image(bitmap,nullptr); UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
        std::vector<unsigned char> storage(size); auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
        Gdiplus::GetImageEncoders(count,size,codecs);
        for (UINT i=0;i<count;++i) { if (wcscmp(codecs[i].MimeType,L"image/png")==0) {
            assert(image.Save(L"tests/configuration_mode_preview.png",&codecs[i].Clsid,nullptr)==Gdiplus::Ok);
        } }
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr,screen);
    // Resizing stretches the old frame, then recreates both layers and sharp glyphs on release.
    SendMessageW(window,WM_ENTERSIZEMOVE,0,0);
    RECT resize_frame={0,0,720,720}; AdjustWindowRect(&resize_frame,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE);
    SetWindowPos(window,nullptr,0,0,resize_frame.right-resize_frame.left,resize_frame.bottom-resize_frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(window,WM_PAINT,0,0); assert(configuration_width==800);
    SendMessageW(window,WM_EXITSIZEMOVE,0,0); SendMessageW(window,WM_PAINT,0,0);
    assert(configuration_width==720 && std::abs(wheel_glyph_scale-0.9f)<0.001f);
    RECT normal={0,0,800,800}; AdjustWindowRect(&normal,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE);
    SetWindowPos(window,nullptr,0,0,normal.right-normal.left,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(window,WM_PAINT,0,0); assert(configuration_width==800 && wheel_glyph_scale==1);
    SendMessageW(window,WM_KEYDOWN,VK_RETURN,0); assert(current_mode=="D Dorian" && configuration_closing);
    wchar_t label[100]={}; GetWindowTextW(mode_label,label,100); assert(std::wstring(label)==L"当前调式：D Dorian");
    assert(rhythm.events.size()==original.events.size());
    for (std::size_t i=0;i<rhythm.events.size();++i) { assert(rhythm.events[i].pitches==original.events[i].pitches); }
    assert(displayed_progression.chords[0].emotion_score==ncnl::roll_emotion_score("D Dorian","C E G"));
    configuration_opened=monotonic_ms()-200; SendMessageW(window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
    assert(!configuration_window && !configuration_dc && !configuration_static_dc && !configuration_animating);
    open_configuration_window(main); window=configuration_window; ShowWindow(window,SW_HIDE);
    pending_tonic=7; pending_mode=5; SendMessageW(window,WM_CLOSE,0,0);
    configuration_opened=monotonic_ms()-200; SendMessageW(window,WM_TIMER,CONFIGURATION_TRANSITION_TIMER,0);
    assert(current_mode=="D Dorian"); // Closing without confirmation discards preview.
    DestroyWindow(main); assert(DeleteFileA(progression_config_path.c_str()));
    Gdiplus::GdiplusShutdown(gdiplus_token);
    std::cout<<"PASS: all 84 mode initializations, fifths wheel/click/drag/rapid scroll, D Dorian apply, existing MIDI preservation, cancel, transitions/buffer cleanup.\n";
}
