#include "../../ui/chord_gui.cpp"
#include <cassert>
#include <iostream>

namespace {
int context_reads=0;
int32_t NCNL_CALL context(void*,NcnlContextV1* value) {
    ++context_reads; value->bpm=120; value->rhythm_events=5; value->midi_notes=15;
    std::strcpy(value->mode,"C Ionian"); return 1;
}
uint32_t NCNL_CALL midi(void*,uint8_t*,uint32_t) { return 0; }
void write(const std::wstring& file,const std::string& text) {
    HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr); assert(handle!=INVALID_HANDLE_VALUE);
    DWORD count=0; assert(WriteFile(handle,text.data(),text.size(),&count,nullptr)); CloseHandle(handle); assert(count==text.size());
}
void screenshot(const std::wstring& file,HWND window,bool config) {
    HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
    HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000); auto previous=SelectObject(dc,bitmap);
    if (config) { draw_configuration_contents(window,dc); }
    else { draw_background(window,dc,true); }
    {
        Gdiplus::Bitmap image(bitmap,nullptr); CLSID codec={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        assert(image.Save(file.c_str(),&codec,nullptr)==Gdiplus::Ok);
    }
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
}
void asset(const std::wstring& file,ncnl::ButtonArtwork& art) {
    assert(art.load(file,true)); Gdiplus::Bitmap image(file.c_str());
    int width=image.GetWidth(),height=image.GetHeight(),x0=width,y0=height,x1=-1,y1=-1;
    Gdiplus::Rect rectangle(0,0,width,height); Gdiplus::BitmapData data={};
    assert(image.LockBits(&rectangle,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)==Gdiplus::Ok);
    for (int y=0;y<height;++y) {
        auto row=static_cast<BYTE*>(data.Scan0)+y*data.Stride;
        for (int x=0;x<width;++x) { if (row[x*4+3]) {
            x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x); y1=std::max(y1,y);
        } }
    }
    image.UnlockBits(&data); auto visible=art.visible_size();
    std::cout<<"alpha != 0: "<<x1-x0+1<<"x"<<y1-y0+1<<"\n";
    assert(visible.Width==x1-x0+1 && visible.Height==y1-y0+1);
}
void mode_label_background(HWND window) {
    RECT bounds={}; GetClientRect(mode_label,&bounds);
    POINT origin={0,0}; MapWindowPoints(mode_label,window,&origin,1);
    HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
    HBITMAP bitmap=CreateCompatibleBitmap(reference,bounds.right,bounds.bottom);
    auto previous=SelectObject(dc,bitmap); assert(ensure_main_background_buffer(window,reference));
    PatBlt(dc,0,0,bounds.right,bounds.bottom,WHITENESS);
    SendMessageW(mode_label,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    assert(GetPixel(dc,1,1)==GetPixel(main_background_dc,origin.x+1,origin.y+1));
    int glyphs=0;
    for (int y=0;y<bounds.bottom;++y) { for (int x=0;x<bounds.right;++x) {
        if (GetPixel(dc,x,y)!=GetPixel(main_background_dc,origin.x+x,origin.y+y)) { ++glyphs; }
    } }
    assert(glyphs>30);
    Gdiplus::Bitmap image(bitmap,nullptr); CLSID codec={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    assert(image.Save(L"tests\\plugin_work\\mode_label.png",&codec,nullptr)==Gdiplus::Ok);
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
}
}
int main() {
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&startup,nullptr)==Gdiplus::Ok);
    std::wostringstream name; name<<L"tests\\plugin_work\\fixture_"<<GetCurrentProcessId();
    auto folder=name.str(); assert(CreateDirectoryW(folder.c_str(),nullptr));
    auto good=folder+L"\\a_inspector"; assert(CreateDirectoryW(good.c_str(),nullptr));
    assert(CopyFileW(L"tests\\plugin_work\\plugin.dll",(good+L"\\plugin.dll").c_str(),TRUE));
    assert(CopyFileW(L"src\\plugins\\examples\\rhythm_inspector\\plugin.ini",(good+L"\\plugin.ini").c_str(),TRUE));
    for (int i=0;i<4;++i) {
        std::wostringstream sub; sub<<folder<<L"\\fake_"<<i; assert(CreateDirectoryW(sub.str().c_str(),nullptr));
        assert(CopyFileW(L"tests\\plugin_work\\plugin.dll",(sub.str()+L"\\plugin.dll").c_str(),TRUE));
        std::ostringstream index; index<<i;
        write(sub.str()+L"\\plugin.ini","[plugin]\nabi=1\nid=fake_"+index.str()+"\nname=扩展\nentry=plugin.dll\n");
    }
    HWND parent=CreateWindowW(L"STATIC",L"",WS_OVERLAPPEDWINDOW,0,0,900,780,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(parent);
    {
        ncnl::PluginManager manager; manager.scan(folder); assert(manager.plugins().size()==6);
        assert(manager.plugins()[1].name==L"节奏观察" && manager.enabled_plugins().size()==1);
        NcnlHostV1 host={sizeof(NcnlHostV1),NCNL_PLUGIN_ABI,nullptr,context,midi}; std::wstring error;
        assert(!manager.activate(1,parent,host,error));
        assert(manager.set_enabled(1,true,error) && manager.activate(1,parent,host,error)); manager.resize(900,780);
        HWND page=GetWindow(parent,GW_CHILD); assert(page && manager.active_index()==1);
        SendMessageW(page,WM_PAINT,0,0); assert(context_reads>0);
        assert(manager.activate(0,parent,host,error) && !IsWindow(page));
        assert(manager.set_enabled(2,true,error) && manager.set_enabled(3,true,error));
        assert(!manager.set_enabled(4,true,error) && manager.enabled_plugins().size()==4);
        assert(!manager.set_enabled(0,false,error));
        assert(!manager.activate(2,parent,host,error) && manager.active_index()==0 && IsWindow(parent));
        ncnl::PluginManager restored; restored.scan(folder); assert(restored.enabled_plugins().size()==4);
        assert(manager.set_enabled(3,false,error) && manager.set_enabled(4,true,error));
        assert(manager.activate(1,parent,host,error)); assert(manager.set_enabled(1,false,error) && manager.active_index()==0);
    }
    DestroyWindow(parent);
    load_application_skins(L"assets");
    asset(L"assets\\skins\\buttons\\ncnl.png",ncnl_button_art);
    asset(L"assets\\skins\\buttons\\plugins_tab.png",plugin_tab_art);
    auto plugin_art_size=plugin_tab_art.visible_size();
    assert(plugin_art_size.Width*55==plugin_art_size.Height*360);
    for (int tab=0;tab<3;++tab) {
        auto bounds=configuration_tab_bounds(tab);
        assert(bounds.Width==240 && std::abs(bounds.Height-55.0f*2/3)<0.001 && bounds.Y==739);
    }
    for (int count=1;count<=4;++count) {
        auto first=plugin_button_bounds(0,count),last=plugin_button_bounds(count-1,count);
        assert(std::abs(first.X+last.GetRight()-1000)<0.01 && first.Width==216);
        assert(first.X>=50 && last.GetRight()<=950);
    }
    WNDCLASSW type={}; type.hInstance=GetModuleHandleW(nullptr); type.lpfnWndProc=window_procedure;
    type.lpszClassName=L"NcnlPluginRegression"; assert(RegisterClassW(&type));
    WNDCLASSW config_type=type; config_type.lpfnWndProc=configuration_window_procedure;
    config_type.lpszClassName=L"NoChordNoLifeConfigurationWindow"; assert(RegisterClassW(&config_type));
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,size.right-size.left,size.bottom-size.top,
        nullptr,nullptr,type.hInstance,nullptr); assert(window);
    mode_label_background(window);
    plugin_manager.scan(folder); std::wstring error; assert(plugin_manager.set_enabled(1,true,error));
    update_plugin_interface(window);
    screenshot(L"tests\\plugin_work\\main.png",window,false);
    open_configuration_window(window); assert(configuration_window);
    configuration_tab=CONFIG_TAB_PLUGINS; configuration_switched=0;
    screenshot(L"tests\\plugin_work\\management.png",configuration_window,true);
    auto transform=configuration_transform(configuration_window); auto tab=configuration_tab_bounds(CONFIG_TAB_PLUGINS);
    SendMessageW(configuration_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(
        static_cast<int>((tab.X+tab.Width/2)*transform.scale+transform.offset_x),
        static_cast<int>((tab.Y+tab.Height/2)*transform.scale+transform.offset_y)));
    assert(configuration_tab==CONFIG_TAB_PLUGINS);
    auto indices=plugin_manager.enabled_plugins(); assert(indices.size()<=4);
    auto button=plugin_button_bounds(1,static_cast<int>(indices.size()));
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(static_cast<int>(button.X+108),950));
    assert(plugin_manager.active_index()==1 && !IsWindowVisible(generate_button));
    NcnlContextV1 live={}; live.size=sizeof(live); assert(plugin_context(nullptr,&live) && live.bpm==BPM);
    auto previous_rhythm=rhythm; assert(!rhythm.events.empty()); rhythm.events[0].pitches={60,64,67};
    uint32_t length=plugin_read_midi(nullptr,nullptr,0); assert(length>=14);
    std::vector<uint8_t> exported(length,0xa5);
    assert(plugin_read_midi(nullptr,exported.data(),length-1)==length && exported[0]==0xa5);
    assert(plugin_read_midi(nullptr,exported.data(),length)==length);
    assert(std::memcmp(exported.data(),"MThd",4)==0); rhythm=previous_rhythm;
    auto native=plugin_button_bounds(0,static_cast<int>(indices.size()));
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(static_cast<int>(native.X+108),950));
    assert(plugin_manager.active_index()==0 && (GetWindowLongPtrW(generate_button,GWL_STYLE)&WS_VISIBLE));
    DestroyWindow(window); plugin_container=nullptr; plugin_manager.shutdown();
    WIN32_FIND_DATAW child={}; HANDLE search=FindFirstFileW((folder+L"\\*").c_str(),&child);
    if (search!=INVALID_HANDLE_VALUE) {
        do {
            std::wstring file=child.cFileName; if (file==L"." || file==L"..") { continue; }
            auto path=folder+L"\\"+file;
            if (child.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                assert(DeleteFileW((path+L"\\plugin.dll").c_str())); assert(DeleteFileW((path+L"\\plugin.ini").c_str()));
                assert(RemoveDirectoryW(path.c_str()));
            } else { assert(DeleteFileW(path.c_str())); }
        } while (FindNextFileW(search,&child)); FindClose(search);
    }
    assert(RemoveDirectoryW(folder.c_str()));
    std::cout<<"通过：中文元数据、可选加载、真实 DLL 页面、ABI 校验、四插件上限、状态保存、禁用卸载、居中排列与界面切换。\n";
}
