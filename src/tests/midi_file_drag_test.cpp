#include "../ui/chord_gui.cpp"
#include <shlobj.h>
#include <cassert>
#include <iostream>

namespace {
std::vector<unsigned char> expected_bytes,header_bytes;
std::wstring drag_path,target_path;
int header_exports=0;
bool accept_drop=true;

HRESULT WINAPI copy_drop(IDataObject* data,IDropSource* source,DWORD allowed,DWORD* effect) {
    assert(allowed==DROPEFFECT_COPY);
    assert(source->QueryContinueDrag(FALSE,MK_LBUTTON)==S_OK);
    assert(source->QueryContinueDrag(FALSE,0)==DRAGDROP_S_DROP);
    assert(source->QueryContinueDrag(TRUE,MK_LBUTTON)==DRAGDROP_S_CANCEL);
    assert(source->QueryContinueDrag(FALSE,MK_LBUTTON|MK_RBUTTON)==DRAGDROP_S_CANCEL);
    assert(source->QueryContinueDrag(FALSE,MK_LBUTTON|MK_MBUTTON)==DRAGDROP_S_CANCEL);
    assert(source->GiveFeedback(DROPEFFECT_COPY)==DRAGDROP_S_USEDEFAULTCURSORS);
    IDataObject* queried=nullptr;
    assert(data->QueryInterface(IID_IDataObject,reinterpret_cast<void**>(&queried))==S_OK);
    queried->Release();
    IEnumFORMATETC* formats=nullptr;
    assert(data->EnumFormatEtc(DATADIR_GET,&formats)==S_OK);
    FORMATETC items[3]={}; ULONG count=0;
    assert(formats->Next(3,items,&count)==S_FALSE && count==2);
    assert(items[0].cfFormat==CF_HDROP && items[1].cfFormat==RegisterClipboardFormatW(L"Preferred DropEffect"));
    assert(formats->Reset()==S_OK && formats->Skip(1)==S_OK);
    IEnumFORMATETC* clone=nullptr; assert(formats->Clone(&clone)==S_OK);
    FORMATETC item={}; assert(clone->Next(1,&item,nullptr)==S_OK && item.cfFormat==items[1].cfFormat);
    assert(clone->Skip(1)==S_FALSE); clone->Release(); formats->Release();
    STGMEDIUM preferred={}; assert(data->GetData(&items[1],&preferred)==S_OK);
    assert(*static_cast<DWORD*>(GlobalLock(preferred.hGlobal))==DROPEFFECT_COPY);
    GlobalUnlock(preferred.hGlobal); ReleaseStgMedium(&preferred);
    STGMEDIUM medium={}; assert(data->GetData(&items[0],&medium)==S_OK);
    HDROP drop=reinterpret_cast<HDROP>(medium.hGlobal);
    assert(DragQueryFileW(drop,0xffffffff,nullptr,0)==1);
    UINT length=DragQueryFileW(drop,0,nullptr,0); std::vector<wchar_t> path(length+1);
    assert(DragQueryFileW(drop,0,path.data(),length+1)==length); drag_path=path.data();
    assert(drag_path.find(L"节奏")!=std::wstring::npos);
    assert(drag_path.find(L"?.mid")==std::wstring::npos);
    assert(read_midi_file(drag_path)==expected_bytes);
    auto memory=static_cast<unsigned char*>(GlobalLock(medium.hGlobal));
    auto names=reinterpret_cast<wchar_t*>(memory+reinterpret_cast<DROPFILES*>(memory)->pFiles);
    assert(names[length]==0 && names[length+1]==0);
    GlobalUnlock(medium.hGlobal); ReleaseStgMedium(&medium);
    auto invalid=items[0]; invalid.dwAspect=DVASPECT_THUMBNAIL;
    assert(data->QueryGetData(&invalid)==DV_E_DVASPECT);
    invalid=items[0]; invalid.lindex=0; assert(data->QueryGetData(&invalid)==DV_E_LINDEX);
    invalid=items[0]; invalid.tymed=TYMED_ISTREAM; assert(data->QueryGetData(&invalid)==DV_E_TYMED);
    if (!accept_drop) { *effect=DROPEFFECT_NONE; return DRAGDROP_S_CANCEL; }
    assert(CopyFileW(drag_path.c_str(),target_path.c_str(),TRUE));
    *effect=DROPEFFECT_COPY; return DRAGDROP_S_DROP;
}

HRESULT capture_header(const std::vector<unsigned char>& bytes,const std::wstring&,DWORD* effect,ncnl::FileDragOperation) {
    ++header_exports; header_bytes=bytes; *effect=DROPEFFECT_NONE; return DRAGDROP_S_CANCEL;
}

void save_preview(Gdiplus::Bitmap& image,const wchar_t* path) {
    UINT count=0,size=0; Gdiplus::GetImageEncodersSize(&count,&size);
    std::vector<unsigned char> storage(size);
    auto codecs=reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    Gdiplus::GetImageEncoders(count,size,codecs);
    for (UINT i=0;i<count;++i) {
        if (wcscmp(codecs[i].MimeType,L"image/png")==0) {
            assert(image.Save(path,&codecs[i].Clsid,nullptr)==Gdiplus::Ok); return;
        }
    }
    assert(false);
}
}

int main() {
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&startup,nullptr)==Gdiplus::Ok);
    std::wostringstream fixture; fixture<<L"tests\\midi_drag_tmp_"<<GetCurrentProcessId();
    wchar_t absolute[MAX_PATH]={}; assert(GetFullPathNameW(fixture.str().c_str(),MAX_PATH,absolute,nullptr));
    std::wstring folder=absolute;
    assert(GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES && CreateDirectoryW(folder.c_str(),nullptr));
    target_path=folder+L"\\拖出.mid";
    ncnl::MidiRhythm exported; exported.length=3;
    exported.events={{0.25,0.5,73,{60,64,67}},{1.5,0.25,88,{62,65,69}}};
    expected_bytes=ncnl::encode_midi(exported,120);
    DWORD effect=99;
    assert(ncnl::drag_midi_file({},L"empty",&effect,copy_drop)==E_INVALIDARG && effect==DROPEFFECT_NONE);
    assert(ncnl::drag_midi_file(expected_bytes,L"节奏?C E G",&effect,copy_drop)==DRAGDROP_S_DROP);
    assert(effect==DROPEFFECT_COPY && read_midi_file(target_path)==expected_bytes);
    assert(GetFileAttributesW(drag_path.c_str())!=INVALID_FILE_ATTRIBUTES);
    // These exact files/directories were allocated by this test's drag only.
    assert(DeleteFileW(drag_path.c_str()));
    assert(RemoveDirectoryW(drag_path.substr(0,drag_path.find_last_of(L'\\')).c_str()));
    accept_drop=false;
    assert(ncnl::drag_midi_file(expected_bytes,L"节奏 cancel",&effect,copy_drop)==DRAGDROP_S_CANCEL);
    assert(effect==DROPEFFECT_NONE && GetFileAttributesW(drag_path.c_str())==INVALID_FILE_ATTRIBUTES);
    assert(GetFileAttributesW(drag_path.substr(0,drag_path.find_last_of(L'\\')).c_str())==INVALID_FILE_ATTRIBUTES);

    WNDCLASSW type={}; type.lpfnWndProc=window_procedure; type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"MidiFileDragRegression"; type.style=CS_DBLCLKS;
    assert(RegisterClassW(&type));
    RECT frame={0,0,1000,1000}; AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        frame.right-frame.left,frame.bottom-frame.top,nullptr,nullptr,type.hInstance,nullptr); assert(window);
    assert(!GetDlgItem(window,1006) && !GetDlgItem(window,1007));
    rhythm.length=4; rhythm.events={{0,0.5,80,{60,64,67}},{1,0.5,90,{62,65,69}},
        {2.25,0.25,76,{64,67,71}},{3,0.5,85,{65,69,72}}};
    rhythm_splits={true,true,true}; rebuild_chord_blocks(); refresh_progression_display();
    undo_history.clear(); auto original=rhythm;
    header_file_export=capture_header;
    POINT header={static_cast<LONG>(timeline_x((block_start(2)+block_end(2))/2)),320};
    assert(piano_header_at_client_point(window,header)==2);
    auto press=[&]() { SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(header.x,header.y)); };
    press(); assert(header_drag_block==2);
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(header.x+50,header.y));
    assert(header_exports==0); // Movement alone does not bypass the hold threshold.
    header_drag_started=monotonic_ms()-HEADER_DRAG_HOLD_MS-1;
    SendMessageW(window,WM_TIMER,HEADER_DRAG_TIMER,0);
    assert(header_exports==1 && header_drag_block==-1 && GetCapture()!=window);
    // Dragging the third header exports all four chords, original timing and loop length.
    assert(header_bytes==ncnl::encode_midi(original,BPM));
    assert(undo_history.empty() && rhythm.events.size()==original.events.size());
    for (std::size_t i=0;i<rhythm.events.size();++i) { assert(rhythm.events[i].pitches==original.events[i].pitches); }
    press(); header_drag_started=monotonic_ms()-HEADER_DRAG_HOLD_MS-1;
    SendMessageW(window,WM_TIMER,HEADER_DRAG_TIMER,0); assert(header_exports==1); // Holding without movement is harmless.
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(header.x,header.y)); assert(header_drag_block==-1);
    press(); SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0); assert(header_drag_block==-1);
    press(); SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(header.x,header.y));
    assert(header_drag_block==-1 && editing_chord==2 && header_exports==1); hide_chord_editor();
    press(); SendMessageW(window,WM_CLEAR_CHORD_HOVER,0,MAKELPARAM(header.x,header.y));
    assert(header_drag_block==-1 && rhythm.events[2].pitches.empty());
    // An empty chord title still exports the other notes in the full roll.
    press(); header_drag_started=monotonic_ms()-HEADER_DRAG_HOLD_MS-1;
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(header.x+50,header.y));
    assert(header_exports==2 && header_bytes==ncnl::encode_midi(rhythm,BPM));
    // Every title has the same export scope; no onset shift or cropping occurs.
    rhythm=original; refresh_progression_display();
    auto complete_bytes=ncnl::encode_midi(rhythm,BPM);
    for (std::size_t i=0;i<chord_blocks.size();++i) {
        POINT at={static_cast<LONG>(timeline_x((block_start(i)+block_end(i))/2)),320};
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(at.x,at.y));
        header_drag_started=monotonic_ms()-HEADER_DRAG_HOLD_MS-1;
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(at.x+50,at.y));
        assert(header_exports==static_cast<int>(i)+3 && header_bytes==complete_bytes);
    }
    for (auto& event:rhythm.events) { event.pitches.clear(); }
    press(); header_drag_started=monotonic_ms()-HEADER_DRAG_HOLD_MS-1;
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(header.x+50,header.y));
    assert(header_exports==6 && header_drag_block==-1); // Entirely empty rolls produce no file.
    SetCapture(window); SendMessageW(window,WM_CLEAR_CHORD_HOVER,0,MAKELPARAM(500,910));
    assert(GetCapture()==window); ReleaseCapture(); // Right-hover capture remains intact without a pending header drag.
    press(); SendMessageW(window,WM_CANCELMODE,0,0); assert(header_drag_block==-1);
    rhythm=original; refresh_progression_display();
    {
        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        auto old=SelectObject(dc,bitmap); draw_background(window,dc,true);
        Gdiplus::Bitmap image(bitmap,nullptr);
        save_preview(image,L"tests/midi_drag_ui_preview.png");
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
    }
    DestroyWindow(window);
    assert(DeleteFileW(target_path.c_str())); assert(RemoveDirectoryW(folder.c_str()));
    Gdiplus::GdiplusShutdown(gdiplus_token);
    std::cout<<"PASS: Unicode CF_HDROP/copy export/cancellation; every header exports the entire MIDI, empty-header export, all-empty guard; hold/double-click/Escape/right-hover isolation and original MIDI preservation.\n";
}
