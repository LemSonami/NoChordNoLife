#include "../chord_gui.cpp"
#include <iostream>

namespace {
int open_calls=0,close_calls=0,reset_calls=0;
bool port_active=false,mapper_missing=false;
std::vector<DWORD> messages;
MMRESULT WINAPI mock_open(LPHMIDIOUT output,UINT id,DWORD_PTR,DWORD_PTR,DWORD) {
    ++open_calls;
    if (mapper_missing && id==static_cast<UINT>(MIDI_MAPPER)) { return MIDIERR_NODEVICE; }
    if (port_active) { return MMSYSERR_ALLOCATED; }
    port_active=true; *output=reinterpret_cast<HMIDIOUT>(1);
    return MMSYSERR_NOERROR;
}
MMRESULT WINAPI mock_close(HMIDIOUT) { ++close_calls; port_active=false; return MMSYSERR_NOERROR; }
MMRESULT WINAPI mock_reset(HMIDIOUT) { ++reset_calls; return MMSYSERR_NOERROR; }
MMRESULT WINAPI mock_message(HMIDIOUT,DWORD message) { messages.push_back(message); return MMSYSERR_NOERROR; }
UINT WINAPI mock_count() { return 1; }
MMRESULT WINAPI silent_real_message(HMIDIOUT output,DWORD message) {
    // Verify real driver calls without producing test sounds.
    if ((message&0xf0)==0x90) { message&=0xffff; }
    return midiOutShortMsg(output,message);
}
void require(bool condition,const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}

int main(int argc,char**) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) { return 1; }
    if (!load_interface_font(L"assets\\res\\font.ttf")) {
        std::cerr<<"custom font load failed\n"; Gdiplus::GdiplusShutdown(gdiplus_token); return 1;
    }
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure;
    type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=L"MidiSharedRegression";
    RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    MidiOutputApi real_api=midi_output_api;
    midi_output_api.open=mock_open; midi_output_api.close=mock_close;
    midi_output_api.reset=mock_reset; midi_output_api.message=mock_message;
    midi_output_api.count=mock_count;
    int result=0;
    try {
        require(window!=nullptr,"window creation failed");
        for (std::size_t i=0;i<4;++i) {
            displayed_progression.chords[i]={"C E G",50}; apply_block_notes(i);
        }
        require(play_key_preview(window,60),"audition failed");
        require(key_preview_pitch==60 && main_background_dirty,"key did not request highlight redraw");
        toggle_midi_playback(window);
        require(midi_playback_thread!=nullptr && open_calls==1,"progression reopened exclusive port");
        Sleep(30); stop_midi_playback();
        require(key_preview_pitch==60 && port_active && close_calls==0 && reset_calls==0,
            "stopping progression interrupted audition or closed device");
        bool playback_on=false,channel_off=false;
        for (DWORD message:messages) {
            playback_on=playback_on || (message&255)==0x90;
            channel_off=channel_off || message==(0xB0u|(123u<<8));
            require(message!=(0xB1u|(123u<<8)),"progression stopped the audition channel");
        }
        require(playback_on && channel_off,"progression MIDI messages missing");
        stop_key_preview();
        require(key_preview_pitch==-1 && messages.back()==(0x81u|(60u<<8)),"audition release failed");
        for (int i=0;i<3;++i) {
            toggle_midi_playback(window); Sleep(20); stop_midi_playback();
        }
        require(open_calls==1 && close_calls==0,"repeated playback reopened port");

        // Three actual keyboard renders: idle, a held black key, a held white key.
        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        HGDIOBJ old=SelectObject(dc,bitmap);
        Gdiplus::Bitmap montage(246,298,PixelFormat32bppARGB);
        Gdiplus::Graphics gallery(&montage);
        COLORREF idle_black=0,idle_white=0;
        for (int frame=0;frame<3;++frame) {
            stop_key_preview();
            if (frame) { require(play_key_preview(window,frame==1?61:60),"preview highlight failed"); }
            draw_background(window,dc,true);
            if (!frame) {
                idle_black=GetPixel(dc,55,607); idle_white=GetPixel(dc,55,633);
                int white_pixels=0;
                for (int y=598;y<618;++y) {
                    for (int x=55;x<99;++x) {
                        COLORREF color=GetPixel(dc,x,y);
                        white_pixels+=GetRValue(color)>220 && GetGValue(color)>220 && GetBValue(color)>220;
                    }
                }
                require(white_pixels>5,"black key name is not white or is outside black key");
            } else if (frame==1) {
                require(GetPixel(dc,55,607)!=idle_black,"black key was not highlighted");
                require(GetPixel(dc,55,633)==idle_white,"black key press modified adjacent white key");
            } else { require(GetPixel(dc,55,633)!=idle_white,"white key was not highlighted"); }
            Gdiplus::Bitmap snapshot(bitmap,nullptr);
            gallery.DrawImage(&snapshot,Gdiplus::Rect(frame*82,0,82,298),50,347,82,298,Gdiplus::UnitPixel);
        }
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        require(montage.Save(L"tests\\keyboard_preview.png",&png,nullptr)==Gdiplus::Ok,"keyboard preview save failed");
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
        stop_key_preview();

        // Close at application shutdown, then exercise a mapper failure + explicit-device fallback.
        midi_output_api.reset(shared_midi_output); midi_output_api.close(shared_midi_output);
        shared_midi_output=nullptr; mapper_missing=true;
        int before=open_calls;
        require(open_shared_midi_output()==MMSYSERR_NOERROR && open_calls==before+2,
            "concrete device fallback failed");
        DestroyWindow(window); window=nullptr;
        require(!port_active && shared_midi_output==nullptr && close_calls==2,"shutdown leaked device");
        midi_output_api=real_api;
        if (argc>1) {
            require(open_shared_midi_output()==MMSYSERR_NOERROR,"real MIDI device failed to open");
            HMIDIOUT shared=shared_midi_output;
            midi_output_api.message=silent_real_message;
            require(play_key_preview(nullptr,61),"real audition could not reuse port");
            toggle_midi_playback(nullptr);
            require(midi_playback_thread && shared_midi_output==shared,"real playback failed after audition");
            Sleep(40); stop_midi_playback(); stop_key_preview();
            midi_output_api.reset(shared_midi_output); midi_output_api.close(shared_midi_output);
            shared_midi_output=nullptr; midi_output_api=real_api;
            std::cout<<"PASS: real Windows MIDI device, audition then playback via one shared handle (silent test)\n";
        }
        std::cout<<"PASS: exclusive-port reuse, repeated playback, channel isolation, fallback, shutdown, key labels/highlights\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    stop_midi_playback(); stop_key_preview();
    if (window) { DestroyWindow(window); }
    if (shared_midi_output) {
        midi_output_api.reset(shared_midi_output); midi_output_api.close(shared_midi_output);
        shared_midi_output=nullptr;
    }
    midi_output_api=real_api;
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    unload_interface_font();
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return result;
}
