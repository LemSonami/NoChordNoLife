// Standalone regression tests; all MIDI fixtures live in a new PID-specific folder.
#include "../preset_library.cpp"
#include <cassert>
#include <iostream>

template<class F> void must_fail(F action) {
    bool failed=false; try { action(); } catch (const std::exception&) { failed=true; }
    assert(failed);
}
int main() {
    ULONG_PTR token=0; Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok);
    std::wostringstream folder_name; folder_name<<L"tests\\preset_library_tmp_"<<GetCurrentProcessId();
    const std::wstring folder=folder_name.str();
    assert(GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES);
    ncnl::MidiRhythm rhythm;
    rhythm.length=4; rhythm.events={{0,0.5,96,{60,64,67}},{1.5,1,80,{62,65,69}}};
    auto bytes=ncnl::encode_midi(rhythm,120);
    {
        ncnl::PresetStore store(folder);
        assert(store.save(L"节奏 A",bytes)==0);
        store.save(L"节奏 B.mid",bytes); store.save(L"节奏 C",bytes);
        must_fail([&]{store.save(L"节奏 A",bytes);});
        must_fail([&]{store.save(L"..\\escape",bytes);});
        must_fail([&]{store.save(L"CON",bytes);});
        must_fail([&]{store.save(L"空文件",{});});
        assert(ncnl::read_bytes(store.path(0))==bytes);
        must_fail([&]{store.rename(0,L"节奏 B");});
        must_fail([&]{store.rename(100,L"越界");});
        store.rename(0,L"改名 A"); store.reorder(2,0);
        ncnl::PresetStore reopened(folder);
        assert(reopened.files()==store.files());
        assert(reopened.files()[0]==L"节奏 C.mid");
        assert(ncnl::read_bytes(reopened.path(1))==bytes);
        ncnl::write_bytes(ncnl::absolute_path(folder)+L"\\外部.midi",bytes,CREATE_NEW);
        reopened.refresh(); assert(reopened.files().back()==L"外部.midi");
    }
    std::wstring loaded;
    ncnl::open_preset_library(nullptr,folder,L"assets\\cursor",nullptr,L"Microsoft YaHei UI",
        [&](const std::wstring& path){ loaded=path; return true; },[&]{return bytes;});
    HWND window=ncnl::preset_library_window(); assert(window);
    ShowWindow(window,SW_HIDE);
    auto click=[&](int x,int y) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    RECT client{}; GetClientRect(window,&client); assert(client.right==1000 && client.bottom==1000);
    SetWindowTextW(ncnl::library->editor,L"界面保存"); click(800,770);
    assert(ncnl::library->store->files().size()==5);
    assert(ncnl::library->status==L"已保存为 MIDI 预设");
    click(70,180); SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(70,180));
    assert(loaded==ncnl::library->store->path(0));
    ncnl::library->load=[](const std::wstring&){ return false; };
    SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(900,180));
    assert(ncnl::library->status==L"预设未加载，原有节奏保留");
    SendMessageW(window,WM_KEYDOWN,VK_F2,0); assert(ncnl::library->renaming==0);
    RECT edit_bounds{}; GetWindowRect(ncnl::library->renamer,&edit_bounds);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&edit_bounds),2);
    assert(edit_bounds.top>=ncnl::LIST_TOP && edit_bounds.bottom<ncnl::LIST_TOP+ncnl::ROW_HEIGHT);
    SetWindowTextW(ncnl::library->renamer,L"取消重命名");
    SendMessageW(ncnl::library->renamer,WM_KEYDOWN,VK_ESCAPE,0);
    assert(ncnl::library->renaming==-1 && ncnl::library->store->files()[0]!=L"取消重命名.mid");
    loaded.clear();
    SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(135,180));
    assert(ncnl::library->renaming==0 && loaded.empty());
    SetWindowTextW(ncnl::library->renamer,L"界面重命名");
    SendMessageW(ncnl::library->renamer,WM_KEYDOWN,VK_RETURN,0);
    assert(ncnl::library->store->files()[0]==L"界面重命名.mid");
    SendMessageW(window,WM_KEYDOWN,VK_F2,0);
    SetWindowTextW(ncnl::library->renamer,L"节奏 B");
    SendMessageW(ncnl::library->renamer,WM_KEYDOWN,VK_RETURN,0);
    assert(ncnl::library->renaming==0 && ncnl::library->store->files()[0]==L"界面重命名.mid");
    SendMessageW(ncnl::library->renamer,WM_KEYDOWN,VK_ESCAPE,0);
    SendMessageW(window,WM_KEYDOWN,VK_F2,0);
    SetWindowTextW(ncnl::library->renamer,L"失焦保存");
    SendMessageW(ncnl::library->renamer,WM_KILLFOCUS,reinterpret_cast<WPARAM>(window),0);
    assert(ncnl::library->renaming==-1 && ncnl::library->store->files()[0]==L"失焦保存.mid");
    SendMessageW(window,WM_KEYDOWN,VK_F2,0);
    SetWindowTextW(ncnl::library->renamer,L"界面重命名");
    SendMessageW(ncnl::library->renamer,WM_KEYDOWN,VK_RETURN,0);
    SendMessageW(window,WM_SETCURSOR,reinterpret_cast<WPARAM>(ncnl::library->editor),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
    assert(GetCursor()==ncnl::preset_library_feedback().cursor(ncnl::AppCursor::text));
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(150,180));
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(150,330));
    assert(ncnl::library->dragging);
    assert(GetCursor()==ncnl::preset_library_feedback().cursor(ncnl::AppCursor::vertical));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(150,330));
    assert(ncnl::library->store->files()[2]==L"界面重命名.mid");
    ncnl::PresetStore ordered(folder); assert(ordered.files()==ncnl::library->store->files());
    // Recycle only a test-created fixture, then hover the same row repeatedly: no cascade.
    std::wstring recycled=ncnl::library->store->path(0);
    SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(150,180));
    assert(GetCursor()==ncnl::preset_library_feedback().cursor(ncnl::AppCursor::unavailable));
    assert(GetFileAttributesW(recycled.c_str())==INVALID_FILE_ATTRIBUTES);
    for (int repeat=0;repeat<10;++repeat) {
        SendMessageW(window,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(155+repeat,185));
    }
    assert(ncnl::library->store->files().size()==4);
    SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(150,180));
    // Deferred square resize, retaining the scale until the gesture ends.
    SendMessageW(window,WM_ENTERSIZEMOVE,0,0);
    RECT frame{}; GetWindowRect(window,&frame); frame.right-=120; frame.bottom-=40;
    SendMessageW(window,WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&frame));
    SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    assert(ncnl::library->scale==1);
    SendMessageW(window,WM_EXITSIZEMOVE,0,0); GetClientRect(window,&client);
    assert(client.right==client.bottom && ncnl::library->scale<1);
    ncnl::library->scale=1; ncnl::select(1);
    ncnl::library->reveal_started=ncnl::transition_ms()-300;
    ncnl::library->status_started=ncnl::transition_ms()-300;
    ncnl::library->window_started=ncnl::transition_ms()-300;
    SendMessageW(window,WM_TIMER,ncnl::TRANSITION_TIMER,0);
    DWORD flags=0; BYTE opacity=0; COLORREF key=0;
    typedef BOOL (WINAPI *GetOpacity)(HWND,COLORREF*,BYTE*,DWORD*);
    auto get_opacity=reinterpret_cast<GetOpacity>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetLayeredWindowAttributes"));
    assert(get_opacity && get_opacity(window,&key,&opacity,&flags) && opacity==255);
    {
        Gdiplus::Bitmap preview(1000,1000,PixelFormat32bppARGB); Gdiplus::Graphics graphics(&preview);
        ncnl::draw(graphics);
        ncnl::rounded(graphics,{50,746,598,50},12,Gdiplus::Color(255,31,43,61));
        ncnl::text(graphics,ncnl::editor_text(),{64,746,570,50},22);
        UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
        std::vector<unsigned char> storage(size);
        auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data()); Gdiplus::GetImageEncoders(count,size,codecs);
        for (UINT i=0;i<count;++i) { if (wcscmp(codecs[i].MimeType,L"image/png")==0) {
            assert(preview.Save(L"tests/preset_library_preview.png",&codecs[i].Clsid,nullptr)==Gdiplus::Ok);
        } }
    }
    auto remaining=ncnl::library->store->files();
    SendMessageW(window,WM_CLOSE,0,0); assert(ncnl::library->closing);
    ncnl::library->window_started=ncnl::transition_ms()-200;
    SendMessageW(window,WM_TIMER,ncnl::TRANSITION_TIMER,0);
    assert(!ncnl::preset_library_window());
    for (const auto& file:remaining) { assert(DeleteFileW((folder+L"\\"+file).c_str())); }
    assert(DeleteFileW((folder+L"\\order.txt").c_str())); assert(RemoveDirectoryW(folder.c_str()));
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"PASS: MIDI roundtrip, double-click load/name editing, F2/Enter/Escape/focus-loss rename, collision guard, order, recycle hover guard, ANI cursors, square resize, fade completion.\n";
}
