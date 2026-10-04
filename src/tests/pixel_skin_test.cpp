#include "../ui/chord_gui.cpp"
#include <cassert>
#include <iostream>

namespace ncnl {
struct PixelSkinTestAccess {
    static unsigned builds(const PixelSkin& skin) { return skin.cache_builds; }
    static Gdiplus::Bitmap* pixels(const PixelSkin& skin) { return skin.pixels.get(); }
    static Gdiplus::Bitmap* cache(const PixelSkin& skin) { return skin.scaled.get(); }
};
}

namespace {
const CLSID PNG_CODEC={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
void save_scene(HBITMAP bitmap,const wchar_t* path) {
    Gdiplus::Bitmap image(bitmap,nullptr);
    assert(image.Save(path,&PNG_CODEC,nullptr)==Gdiplus::Ok);
}
void clear_skins() {
    background_image.reset();
    pentagon_image.reset(); button_artworks.reset();
    for (auto& skin:configuration_skins) { skin.reset(); }
}
}

int main() {
    Gdiplus::GdiplusStartupInput input;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)==Gdiplus::Ok);
    std::wostringstream name; name<<L"tests\\pixel_skin_tmp_"<<GetCurrentProcessId();
    const auto directory=name.str();
    assert(GetFileAttributesW(directory.c_str())==INVALID_FILE_ATTRIBUTES);
    assert(CreateDirectoryW(directory.c_str(),nullptr));
    const auto file=directory+L"\\crop.png",moved=directory+L"\\moved.png";
    {
        Gdiplus::Bitmap fixture(8,4,PixelFormat32bppARGB);
        for (int y=0;y<4;++y) { for (int x=0;x<8;++x) {
            fixture.SetPixel(x,y,x<2 ? Gdiplus::Color(255,255,0,0) : x>=6
                ? Gdiplus::Color(255,0,0,255) : Gdiplus::Color(255,0,255,0));
        } }
        assert(fixture.Save(file.c_str(),&PNG_CODEC,nullptr)==Gdiplus::Ok);
    }
    {
        ncnl::PixelSkin skin; assert(skin.load(file));
        assert(MoveFileW(file.c_str(),moved.c_str())); // No source file handle retained after decoding.
        Gdiplus::Bitmap canvas(16,16,PixelFormat32bppARGB); Gdiplus::Graphics graphics(&canvas);
        assert(skin.draw(graphics,4,4,0.4f));
        auto cached=ncnl::PixelSkinTestAccess::cache(skin);
        for (int y=0;y<4;++y) { for (int x=0;x<4;++x) {
            Gdiplus::Color pixel; cached->GetPixel(x,y,&pixel);
            assert(std::abs(static_cast<int>(pixel.GetA())-102)<=1 && pixel.GetR()==0 && pixel.GetB()==0);
            assert(pixel.GetG()>250); // Center 1:1 crop, not squeezed red/green/blue stripes.
        } }
        for (int i=0;i<30;++i) { assert(skin.draw(graphics,4,4,0.4f)); }
        assert(ncnl::PixelSkinTestAccess::builds(skin)==1 && ncnl::PixelSkinTestAccess::cache(skin)==cached);
        assert(skin.draw(graphics,8,8,0.4f) && ncnl::PixelSkinTestAccess::builds(skin)==2);
        assert(skin.draw(graphics,8,8,1.0f) && ncnl::PixelSkinTestAccess::builds(skin)==3);
        assert(!skin.draw(graphics,0,8,1.0f)); skin.reset(); assert(!skin);
        assert(!skin.load(directory+L"\\missing.png") && !skin.draw(graphics,8,8,1));
    }
    // Retired bg.png files must not be migrated, loaded, or recreated.
    assert(CreateDirectoryW((directory+L"\\skins").c_str(),nullptr));
    auto fallback=directory+L"\\skins\\bg.png"; assert(CopyFileW(moved.c_str(),fallback.c_str(),TRUE));
    auto legacy=directory+L"\\bg.png"; assert(CopyFileW(moved.c_str(),legacy.c_str(),TRUE));
    load_application_skins(directory); assert(!background_image && !configuration_skins[0] && !configuration_skins[1]);
    assert(GetFileAttributesW(legacy.c_str())!=INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileW(legacy.c_str()));
    clear_skins(); load_application_skins(L"assets");
    assert(background_image && configuration_skins[CONFIG_TAB_MODE] && configuration_skins[CONFIG_TAB_RADAR]);
    assert(pentagon_image);
    for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) { assert(button_artworks[static_cast<ncnl::ButtonArt>(i)]); }
    for (auto* skin:{&background_image,&configuration_skins[0],&configuration_skins[1]}) {
        auto image=ncnl::PixelSkinTestAccess::pixels(*skin);
        assert(image->GetWidth()==image->GetHeight());
    }
    assert(load_interface_font(L"assets\\res\\font.ttf"));
    arrow_image=load_png(L"assets\\arrow.png");
    for (std::size_t i=0;i<emotion_images.size();++i) {
        std::wostringstream path; path<<L"assets\\chord_emotion\\"<<i+1;
        emotion_images[i]=load_emotion_icon(path.str()); initialize_emotion_animation(i,monotonic_ms());
    }
    WNDCLASSW type={}; type.lpfnWndProc=window_procedure; type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"PixelSkinRegression"; assert(RegisterClassW(&type));
    RECT frame={0,0,1000,1000}; AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);
    HWND main=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,type.hInstance,nullptr); assert(main);
    WNDCLASSW settings={}; settings.lpfnWndProc=configuration_window_procedure;
    settings.hInstance=type.hInstance; settings.lpszClassName=L"NoChordNoLifeConfigurationWindow";
    assert(RegisterClassW(&settings));
    std::ostringstream config; config<<"tests/pixel_skin_config_tmp_"<<GetCurrentProcessId()<<".json";
    progression_config_path=config.str();
    assert(GetFileAttributesA(progression_config_path.c_str())==INVALID_FILE_ATTRIBUTES);
    for (std::size_t i=0;i<chord_blocks.size();++i) {
        displayed_progression.chords[i]={"C E G",50}; apply_block_notes(i);
    }
    HDC reference=GetDC(main),dc=CreateCompatibleDC(reference);
    HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000); auto old=SelectObject(dc,bitmap);
    draw_background(main,dc,true);
    // Render the main window's owner-drawn buttons at their current real positions.
    for (const auto& control:controls) {
        wchar_t kind[32]={}; GetClassNameW(control.window,kind,32);
        if (wcscmp(kind,L"Button")!=0) { continue; }
        DRAWITEMSTRUCT item={}; item.CtlType=ODT_BUTTON; item.CtlID=GetDlgCtrlID(control.window);
        item.hwndItem=control.window; item.hDC=dc; item.itemAction=ODA_DRAWENTIRE;
        GetWindowRect(control.window,&item.rcItem);
        MapWindowPoints(nullptr,main,reinterpret_cast<POINT*>(&item.rcItem),2);
        draw_square_button(&item);
    }
    { Gdiplus::Graphics graphics(dc); draw_centered_text(graphics,get_window_text(mode_label),{45,32,420,52},27,true); }
    save_scene(bitmap,L"tests/pixel_skin_main_preview.png");
    const auto builds=ncnl::PixelSkinTestAccess::builds(background_image);
    for (int i=0;i<8;++i) { draw_background(main,dc,true); }
    assert(ncnl::PixelSkinTestAccess::builds(background_image)==builds);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(main,reference);

    open_configuration_window(main); HWND window=configuration_window; assert(window); ShowWindow(window,SW_HIDE);
    configuration_switched=0; reference=GetDC(window); dc=CreateCompatibleDC(reference);
    bitmap=CreateCompatibleBitmap(reference,800,800); old=SelectObject(dc,bitmap);
    const auto original_weights=ncnl::progression_weights().values;
    for (int tab=0;tab<2;++tab) {
        configuration_tab=tab; draw_configuration_contents(window,dc);
        auto count=ncnl::PixelSkinTestAccess::builds(configuration_skins[tab]);
        for (int i=0;i<8;++i) { draw_configuration_contents(window,dc); }
        assert(count==ncnl::PixelSkinTestAccess::builds(configuration_skins[tab]));
        save_scene(bitmap,tab==0 ? L"tests/pixel_skin_mode_preview.png" : L"tests/pixel_skin_radar_preview.png");
    }
    assert(ncnl::progression_weights().values==original_weights);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
    DestroyWindow(window); DestroyWindow(main);
    assert(DeleteFileA(progression_config_path.c_str()));
    for (auto& glyph:wheel_glyphs) { glyph.reset(); }
    arrow_image.reset();
    for (auto& image:emotion_images) { image.reset(); }
    for (auto& animation:emotion_animations) { animation.frames.clear(); }
    clear_skins(); unload_interface_font();
    assert(DeleteFileW(moved.c_str())); assert(DeleteFileW(fallback.c_str()));
    assert(RemoveDirectoryW((directory+L"\\skins").c_str())); assert(RemoveDirectoryW(directory.c_str()));
    Gdiplus::GdiplusShutdown(gdiplus_token);
    std::cout<<"PASS: retired bg.png ignored; decoded/unlocked images, 1:1 crop, 40% alpha, reusable cache/fallback; all button assets; main/mode/original radar UI previews and unchanged weights.\n";
}
