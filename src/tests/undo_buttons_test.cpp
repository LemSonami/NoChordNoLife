#include "../ui/chord_gui.cpp"
#include <iostream>

void require(bool ok,const char* message) {
    if (!ok) { throw std::runtime_error(message); }
}
POINT center(const Gdiplus::RectF& box) {
    return {static_cast<LONG>(box.X+box.Width/2),static_cast<LONG>(box.Y+box.Height/2)};
}
int main() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) { return 1; }
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure;
    type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=L"UndoButtonsRegression";
    RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    int result=0;
    try {
        require(window!=nullptr,"window creation failed");
        for (std::size_t i=0;i<4;++i) {
            displayed_progression.chords[i]={"C E G",50}; apply_block_notes(i);
        }
        undo_history.clear();
        auto original=rhythm.events[0].pitches;
        int preset=slot_presets[0];
        clear_chord_at_client_point(window,center(emotion_bounds(0)));
        require(slot_presets[0]==-1 && undo_history.size()==1,"preset clear not recorded");
        clear_chord_at_client_point(window,center(emotion_bounds(0)));
        require(undo_history.size()==1,"empty clear recorded a no-op");
        undo_last_edit(); require(slot_presets[0]==preset,"preset undo failed");
        int pitch=original[0];
        clear_chord_at_client_point(window,center(visual_note_bounds(rhythm.events[0],pitch)));
        require(rhythm.events[0].pitches.size()+1==original.size(),"note erase failed");
        undo_last_edit(); require(rhythm.events[0].pitches==original && dissolving_notes.empty(),"note undo failed");
        merge_rhythm_boundary(0);
        require(chord_blocks.size()==3,"merge failed");
        undo_last_edit(); require(chord_blocks.size()==4 && rhythm_splits[0],"split undo failed");
        auto note=center(visual_note_bounds(rhythm.events[0],pitch));
        require(begin_midi_note_drag(window,note),"drag failed to begin");
        auto target=center(visual_note_bounds(rhythm.events[0],pitch+1));
        update_midi_note_drag(window,target);
        target=center(visual_note_bounds(rhythm.events[0],pitch+2));
        end_midi_note_drag(window,target);
        require(undo_history.size()==1,"one drag recorded multiple edits");
        undo_last_edit(); require(rhythm.events[0].pitches==original,"drag undo failed");
        begin_chord_edit(window,0); SetWindowTextW(chord_editor,L"D F A");
        require(commit_chord_editor(true),"header commit failed");
        undo_last_edit(); require(displayed_progression.chords[0].notes=="C E G" &&
            rhythm.events[0].pitches==original,"header undo failed");
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_PRESET_BASE+4,BN_CLICKED),0);
        require(slot_presets[0]==4,"preset apply failed");
        undo_last_edit(); require(slot_presets[0]==preset,"preset apply undo failed");
        create_midi_note_at_client_point(window,center(visual_note_bounds(rhythm.events[0],61)));
        undo_last_edit(); require(rhythm.events[0].pitches==original,"note create undo failed");
        clear_chord_at_client_point(window,{200,320});
        require(displayed_progression.chords[0].notes.empty(),"header clear failed");
        undo_last_edit(); require(rhythm.events[0].pitches==original &&
            displayed_progression.chords[0].notes=="C E G","block clear undo failed");
        merge_rhythm_boundary(0); undo_history.clear();
        toggle_rhythm_split(window,{static_cast<LONG>(timeline_x(event_boundary(1))),500});
        require(chord_blocks.size()==4,"split creation failed");
        undo_last_edit(); require(chord_blocks.size()==3 && !rhythm_splits[0],"split creation undo failed");
        remember_edit(); rhythm.events[0].duration=0.5;
        std::size_t events=rhythm.events.size();
        create_midi_note_at_client_point(window,{static_cast<LONG>(timeline_x(0.75)),630});
        require(rhythm.events.size()==events+1,"rest insertion failed");
        undo_last_edit(); require(rhythm.events.size()==events && rhythm.events[0].duration==0.5,
            "rest insertion undo failed");
        undo_last_edit(); require(rhythm.events[0].duration>0.5,"multi-step undo failed");
        auto chords=displayed_progression.chords;
        slot_presets.assign(chord_blocks.size(),-1);
        generate_and_show(window);
        require(undo_history.size()==1,"generation not recorded");
        undo_last_edit(); require(displayed_progression.chords[0].notes==chords[0].notes &&
            rhythm.events[0].pitches==original,"generation undo failed");
        undo_last_edit(); require(undo_history.empty(),"empty undo failed");

        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        CLSID gif={0x557cf402,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        Gdiplus::Bitmap gif_bitmap(2,2),png_bitmap(4,4);
        require(gif_bitmap.Save(L"tests\\emotion_priority_test.gif",&gif,nullptr)==Gdiplus::Ok &&
            png_bitmap.Save(L"tests\\emotion_priority_test.png",&png,nullptr)==Gdiplus::Ok,"test images save failed");
        auto icon=load_emotion_icon(L"tests\\emotion_priority_test");
        require(icon && icon->GetWidth()==2,"GIF was not preferred");
        icon.reset(); DeleteFileW(L"tests\\emotion_priority_test.gif");
        icon=load_emotion_icon(L"tests\\emotion_priority_test");
        require(icon && icon->GetWidth()==4,"PNG fallback failed"); icon.reset();
        Gdiplus::Bitmap source(6,2),dest(2,2);
        for (int y=0;y<2;++y) { for (int x=0;x<6;++x) {
            source.SetPixel(x,y,(x==2 || x==3)?Gdiplus::Color::Blue:Gdiplus::Color::Red);
        } }
        { Gdiplus::Graphics graphics(&dest); graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            draw_square_icon(graphics,&source,Gdiplus::RectF(0,0,2,2)); }
        Gdiplus::Color pixel; dest.GetPixel(1,1,&pixel);
        require(pixel.GetB()>200 && pixel.GetR()<30,"icon not center-cropped");
        for (int i=0;i<5;++i) {
            std::wostringstream path; path<<L"assets\\chord_emotion\\"<<i+1;
            emotion_images[i]=load_emotion_icon(path.str());
            require(emotion_images[i]!=nullptr,"project GIF load failed");
            initialize_emotion_animation(i,1000);
            auto& animation=emotion_animations[i];
            require(animation.delays.size()>1,"project GIF is not animated");
            require(!advance_emotion_animation(i,animation.next_frame-1),"GIF advanced too early");
            require(advance_emotion_animation(i,animation.next_frame) && animation.frame==1,
                "GIF did not advance on its deadline");
            ULONGLONG boundary=animation.next_frame;
            require(advance_emotion_animation(i,boundary+animation.cycle*1000),
                "GIF did not skip delayed cycles");
            std::cout<<"GIF "<<i+1<<": "<<animation.delays.size()<<" frames\n";
        }
        update_emotion_animation_timer(true);
        ULONGLONG due=emotion_animations[0].next_frame;
        interactive_resize=true; update_emotion_animation_timer();
        require(emotion_animations[0].next_frame==due,"pause changed GIF timing");
        interactive_resize=false; update_emotion_animation_timer(true);
        require(emotion_animations[0].next_frame>=monotonic_ms(),"GIF resume deadline invalid");
        ULONGLONG gif_start=monotonic_ms(); bool animated_on_timer=false;
        UINT initial_frame=emotion_animations[0].frame;
        MSG message{};
        while (monotonic_ms()-gif_start<600) {
            if (PeekMessageW(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE)) {
                DispatchMessageW(&message);
                animated_on_timer=animated_on_timer || emotion_animations[0].frame!=initial_frame;
            } else { Sleep(1); }
        }
        require(animated_on_timer,"GIF did not animate through the window timer");
        undo_history.clear();
        for (int edit=0;edit<25;++edit) { active_slot=edit; remember_edit(); }
        require(undo_history.size()==20 && undo_history.front().active==5,
            "history does not retain exactly the latest 20 edits");
        for (int edit=24;edit>=5;--edit) { undo_last_edit(); require(active_slot==edit,"history ordering failed"); }
        require(undo_history.empty(),"history exceeded 20 undos");
        active_slot=0;
        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        HGDIOBJ old=SelectObject(dc,bitmap);
        draw_background(window,dc,true);
        for (int i=0;i<5;++i) {
            int saved=SaveDC(dc); SetViewportOrgEx(dc,210+145*i,125,nullptr);
            DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.CtlID=ID_PRESET_BASE+i;
            item.hwndItem=preset_buttons[i]; item.hDC=dc; item.rcItem={0,0,115,115};
            draw_square_button(&item); RestoreDC(dc,saved);
        }
        { Gdiplus::Bitmap snapshot(bitmap,nullptr);
            std::unique_ptr<Gdiplus::Bitmap> crop(snapshot.Clone(200,115,745,135,PixelFormat32bppARGB));
            require(crop->Save(L"tests\\undo_buttons_preview.png",&png,nullptr)==Gdiplus::Ok,"preview failed"); }
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
        std::cout<<"PASS: undo/20-step limit, GIF priority/PNG fallback, frame timing/loop skipping/timer dispatch, center crop, buffered button rendering\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    DeleteFileW(L"tests\\emotion_priority_test.gif"); DeleteFileW(L"tests\\emotion_priority_test.png");
    discard_note_effects(); stop_animation_clock(); if (window) { DestroyWindow(window); }
    for (auto& image:emotion_images) { image.reset(); }
    for (auto& animation:emotion_animations) { animation.frames.clear(); }
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    Gdiplus::GdiplusShutdown(gdiplus_token); return result;
}
