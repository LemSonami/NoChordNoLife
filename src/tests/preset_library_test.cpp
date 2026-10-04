// Standalone regression tests; all MIDI fixtures live in a new PID-specific folder.
#include "../ui/preset_library.cpp"
#include <cassert>
#include <iostream>

template<class F> void must_fail(F action) {
    bool failed=false; try { action(); } catch (const std::exception&) { failed=true; }
    assert(failed);
}
void control_key(HWND window,WPARAM key,LPARAM flags=0) {
    BYTE previous[256]={},pressed[256]={};
    assert(GetKeyboardState(previous)); std::copy(previous,previous+256,pressed);
    pressed[VK_CONTROL]=pressed[VK_LCONTROL]=0x80; assert(SetKeyboardState(pressed));
    SendMessageW(window,WM_KEYDOWN,key,flags);
    assert(SetKeyboardState(previous));
}
void verify_name_layout(HWND window) {
    RECT bounds{}; GetWindowRect(ncnl::library->editor,&bounds);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&bounds),2);
    int y=static_cast<int>(ncnl::SAVE_Y*ncnl::library->scale),height=static_cast<int>(50*ncnl::library->scale);
    assert(std::abs(bounds.top+bounds.bottom-(2*y+height))<=1);
    assert(bounds.top>=y && bounds.bottom<=y+height);
    LOGFONTW font{}; assert(GetObjectW(ncnl::library->edit_font,sizeof(font),&font));
    assert(font.lfHeight==-static_cast<int>(30*ncnl::library->scale));
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
        [&](const std::wstring& path){ loaded=path; return true; },[&]{return bytes;},L"assets\\skins\\preset.png",L"assets\\skins\\buttons");
    HWND window=ncnl::preset_library_window(); assert(window);
    assert(ncnl::library->background);
    assert(ncnl::library->buttons[ncnl::ButtonArt::Refresh] && ncnl::library->buttons[ncnl::ButtonArt::Save]);
    ShowWindow(window,SW_HIDE);
    auto click=[&](int x,int y) {
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    };
    RECT client{}; GetClientRect(window,&client); assert(client.right==1000 && client.bottom==1000);
    verify_name_layout(window);
    SetWindowTextW(ncnl::library->editor,L"名称全选测试");
    SendMessageW(ncnl::library->editor,EM_SETSEL,2,2);
    control_key(ncnl::library->editor,'A');
    DWORD start=0,end=0;
    SendMessageW(ncnl::library->editor,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    assert(start==0 && end==6);
    SendMessageW(ncnl::library->editor,WM_CHAR,1,0);
    assert(ncnl::editor_text()==L"名称全选测试");
    SetWindowTextW(ncnl::library->editor,L"界面保存"); click(800,ncnl::SAVE_Y+25);
    assert(ncnl::library->store->files().size()==5);
    assert(ncnl::library->status==L"已保存为 MIDI 预设");
    click(70,180); SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(70,180));
    assert(loaded==ncnl::library->store->path(0));
    ncnl::library->load=[](const std::wstring&){ return false; };
    SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(900,180));
    assert(ncnl::library->status==L"预设未加载，原有节奏保留");
    assert(ncnl::library->status_error); // Failed operations remain visible without footer hints.
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
    auto before_delete=ncnl::library->store->files();
    SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(150,180));
    assert(GetCursor()==ncnl::preset_library_feedback().cursor(ncnl::AppCursor::unavailable));
    assert(GetFileAttributesW(recycled.c_str())==INVALID_FILE_ATTRIBUTES);
    for (int repeat=0;repeat<10;++repeat) {
        SendMessageW(window,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(155+repeat,185));
    }
    assert(ncnl::library->store->files().size()==4);
    SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(150,180));
    // Collision protection preserves both the new file and the deletion history.
    auto duplicate=ncnl::library->store->save(ncnl::PresetStore::display_name(before_delete[0]),bytes);
    must_fail([&]{ncnl::library->store->undo_recycle();});
    assert(ncnl::read_bytes(ncnl::library->store->path(duplicate))==bytes);
    assert(DeleteFileW(ncnl::library->store->path(duplicate).c_str()));
    ncnl::library->store->refresh();
    control_key(window,'Z');
    assert(ncnl::library->store->files()==before_delete && ncnl::read_bytes(recycled)==bytes);
    assert(ncnl::library->selected==0 && ncnl::library->store->undo_recycle()==-1);
    // Text editing still uses native text undo, rather than restoring another preset.
    SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(150,180));
    SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(150,180));
    SetWindowTextW(ncnl::library->editor,L"原名称");
    SendMessageW(ncnl::library->editor,EM_SETSEL,0,-1);
    SendMessageW(ncnl::library->editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"修改名称"));
    assert(SendMessageW(ncnl::library->editor,EM_CANUNDO,0,0));
    control_key(ncnl::library->editor,'Z');
    SendMessageW(ncnl::library->editor,WM_CHAR,26,0);
    assert(ncnl::editor_text()==L"原名称" && ncnl::library->store->files().size()==4);
    control_key(window,'Z',1L<<30); // Held shortcut must not repeatedly pop history.
    assert(ncnl::library->store->files().size()==4);
    control_key(window,'Z'); assert(ncnl::library->store->files()==before_delete);
    // Consecutive deletes restore in reverse order, including the original ordering.
    ncnl::library->store->recycle(1,window); ncnl::library->store->recycle(2,window);
    control_key(window,'Z'); control_key(window,'Z');
    assert(ncnl::library->store->files()==before_delete);
    ncnl::PresetStore restored_order(folder); assert(restored_order.files()==before_delete);
    // Deferred square resize, retaining the scale until the gesture ends.
    SendMessageW(window,WM_ENTERSIZEMOVE,0,0);
    RECT frame{}; GetWindowRect(window,&frame); frame.right-=120; frame.bottom-=40;
    SendMessageW(window,WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&frame));
    SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    assert(ncnl::library->scale==1);
    SendMessageW(window,WM_EXITSIZEMOVE,0,0); GetClientRect(window,&client);
    assert(client.right==client.bottom && ncnl::library->scale<1);
    verify_name_layout(window);
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
        ncnl::rounded(graphics,{50,ncnl::SAVE_Y,598,50},12,Gdiplus::Color(255,31,43,61));
        ncnl::text(graphics,ncnl::editor_text(),{64,ncnl::SAVE_Y,570,50},ncnl::NAME_EDIT_SIZE);
        UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
        std::vector<unsigned char> storage(size);
        auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data()); Gdiplus::GetImageEncoders(count,size,codecs);
        for (UINT i=0;i<count;++i) { if (wcscmp(codecs[i].MimeType,L"image/png")==0) {
            assert(preview.Save(L"tests/preset_library_preview.png",&codecs[i].Clsid,nullptr)==Gdiplus::Ok);
        } }
    }
    ncnl::library->store->recycle(0,window);
    SendMessageW(window,WM_CLOSE,0,0); assert(ncnl::library->closing);
    ncnl::library->window_started=ncnl::transition_ms()-200;
    SendMessageW(window,WM_TIMER,ncnl::TRANSITION_TIMER,0);
    assert(!ncnl::preset_library_window());
    ncnl::open_preset_library(nullptr,folder,L"assets\\cursor",nullptr,L"Microsoft YaHei UI",
        [](const std::wstring&){return true;},[&]{return bytes;});
    window=ncnl::preset_library_window(); ShowWindow(window,SW_HIDE);
    control_key(window,'Z');
    assert(ncnl::library->store->files()==before_delete);
    auto remaining=ncnl::library->store->files(); ncnl::close_preset_library();
    for (const auto& file:remaining) { assert(DeleteFileW((folder+L"\\"+file).c_str())); }
    assert(DeleteFileW((folder+L"\\order.txt").c_str())); assert(RemoveDirectoryW(folder.c_str()));
    // An empty library places both large messages at the preview panel's center.
    const std::wstring empty_folder=folder+L"_empty";
    assert(GetFileAttributesW(empty_folder.c_str())==INVALID_FILE_ATTRIBUTES);
    ncnl::open_preset_library(nullptr,empty_folder,L"assets\\cursor",nullptr,L"Microsoft YaHei UI",
        [](const std::wstring&){return true;},[&]{return bytes;},L"assets\\skins\\preset.png",L"assets\\skins\\buttons");
    ShowWindow(ncnl::preset_library_window(),SW_HIDE);
    assert(ncnl::library->store->files().empty());
    wchar_t title[100]={}; GetWindowTextW(ncnl::preset_library_window(),title,100);
    assert(std::wstring(title)==L"NoChordNoLife · 节奏型预设");
    {
        Gdiplus::Bitmap preview(1000,1000,PixelFormat32bppARGB); Gdiplus::Graphics graphics(&preview);
        ncnl::draw(graphics);
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        assert(preview.Save(L"tests/preset_empty_preview.png",&png,nullptr)==Gdiplus::Ok);
    }
    ncnl::close_preset_library();
    assert(RemoveDirectoryW(empty_folder.c_str()));
    // The preset deletion stack shares the app's 20-step retention policy.
    const std::wstring limit_folder=folder+L"_limit";
    {
        ncnl::PresetStore limited(limit_folder);
        for (int i=0;i<21;++i) {
            std::wostringstream name; name<<L"limit_"<<i;
            limited.save(name.str(),bytes);
        }
        auto original=limited.files();
        for (int i=0;i<21;++i) { limited.recycle(0,nullptr); }
        assert(limited.files().empty());
        for (int i=0;i<20;++i) { assert(limited.undo_recycle()==0); }
        assert(limited.undo_recycle()==-1);
        original.erase(original.begin()); assert(limited.files()==original);
        for (const auto& file:limited.files()) { assert(DeleteFileW((limit_folder+L"\\"+file).c_str())); }
        assert(DeleteFileW((limit_folder+L"\\order.txt").c_str()));
    }
    assert(RemoveDirectoryW(limit_folder.c_str()));
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"PASS: MIDI roundtrip, rename/order, centered enlarged name editor/Ctrl+A, native text undo, Ctrl+Z deletion restore/order/collision/reopen/20-step limit, recycle hover guard, cursors, square resize, fade completion.\n";
}
