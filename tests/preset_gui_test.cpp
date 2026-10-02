#include "../chord_gui.cpp"
#include <cassert>
#include <iostream>

int main() {
    Gdiplus::GdiplusStartupInput input;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)==Gdiplus::Ok);
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure;
    type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=L"PresetIntegrationRegression";
    RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    assert(window);
    HWND button=GetDlgItem(window,ID_PRESET_ACTION); RECT bounds{}; GetClientRect(button,&bounds);
    assert(bounds.right>bounds.bottom*2);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_PRESET_ACTION,BN_CLICKED),reinterpret_cast<LPARAM>(button));
    HWND presets=ncnl::preset_library_window(); assert(presets && GetWindow(presets,GW_OWNER)==window);
    ShowWindow(presets,SW_HIDE);
    ncnl::MidiRhythm fixture; fixture.length=4;
    fixture.events={{0,0.5,90,{60,64,67}},{1.5,0.25,80,{62}},{2,0.5,90,{65}}};
    std::wostringstream name; name<<L"tests\\preset_gui_tmp_"<<GetCurrentProcessId();
    const auto folder=name.str(); assert(GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES);
    ncnl::PresetStore store(folder); auto index=store.save(L"integration",ncnl::encode_midi(fixture,BPM));
    auto before=rhythm; undo_history.clear();
    assert(import_midi_file(presets,store.path(index)));
    assert(rhythm.events.size()==3 && rhythm.events[0].pitches==std::vector<int>{60});
    assert(chord_blocks.size()==1 && undo_history.size()==1);
    undo_last_edit(); assert(rhythm.events.size()==before.events.size());
    // Library mouse input uses its own transparent feedback layer and the shared clock.
    MSG click{}; click.hwnd=presets; click.message=WM_LBUTTONDOWN; click.wParam=MK_LBUTTON;
    click.lParam=MAKELPARAM(500,700); track_mouse_feedback_message(click);
    assert(ncnl::preset_library_feedback().active() && animation_clock);
    ncnl::preset_library_feedback().clear(); update_animation_clock();
    ncnl::close_preset_library(); assert(!ncnl::preset_library_window());
    assert(DeleteFileW(store.path(index).c_str())); assert(DeleteFileW((folder+L"\\order.txt").c_str()));
    assert(RemoveDirectoryW(folder.c_str()));
    DestroyWindow(window); assert(!ncnl::preset_library_window());
    Gdiplus::GdiplusShutdown(gdiplus_token);
    std::cout<<"PASS: rectangular preset button, owned library launch, rhythm import/undo, shared effect clock and cleanup.\n";
}
