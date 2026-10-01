#include "../chord_gui.cpp"
#include <iostream>

void require(bool condition,const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

int main() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) { return 1; }
    if (!load_interface_font(L"assets\\res\\font.ttf")) {
        std::cerr<<"custom font load failed\n"; Gdiplus::GdiplusShutdown(gdiplus_token); return 1;
    }
    WNDCLASSW type{};
    type.lpfnWndProc=window_procedure; type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"NoteEffectsRegression";
    RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    int result=0;
    try {
        require(window!=nullptr,"window creation failed");
        require(keyboard_pitch_at_client_point(window,{80,633})==60,"C4 key hit failed");
        require(keyboard_pitch_at_client_point(window,{80,359})==71,"B4 key hit failed");
        require(keyboard_pitch_at_client_point(window,{140,633})==-1,"grid treated as keyboard");
        require(keyboard_pitch_at_client_point(window,{80,320})==-1,"header treated as keyboard");
        require(keyboard_pitch_at_client_point(window,{80,607})==61,"black overlay hit failed");
        require(keyboard_pitch_at_client_point(window,{120,600})==62,"upper exposed white key hit failed");
        require(keyboard_pitch_at_client_point(window,{120,615})==60,"lower exposed white key hit failed");
        auto black=keyboard_key_bounds(61),lower=keyboard_key_bounds(60),upper=keyboard_key_bounds(62);
        require(black.Y<lower.Y && black.GetBottom()>lower.Y &&
            std::abs(upper.GetBottom()-lower.Y)<0.01,"black key is not overlapping two white keys");
        HDC font_dc=GetDC(window);
        auto old_font=SelectObject(font_dc,normal_font);
        wchar_t face[128]={}; GetTextFaceW(font_dc,128,face);
        SelectObject(font_dc,old_font); ReleaseDC(window,font_dc);
        require(face[0] && lstrcmpiW(face,L"Microsoft YaHei UI")!=0,"native controls did not use asset font");
        key_preview_gesture=true; key_preview_pitch=60;
        SendMessageW(window,WM_CAPTURECHANGED,0,0);
        require(!key_preview_gesture && key_preview_pitch==-1,"capture loss left audition active");
        // Unbalanced block durations: cards must align with real split boundaries.
        rhythm.events={{0,0.5,90,{}},{1,0.5,90,{}},{2,0.5,90,{}},
            {3,0.5,90,{}},{4,0.5,90,{}}};
        rhythm.length=6;
        chord_blocks={{0,1},{1,4},{4,5}};
        rhythm_splits={true,false,false,true};
        slot_presets={0,2,4}; displayed_progression.chords.assign(3,{"C E G",50});
        for (std::size_t i=0;i<3;++i) { apply_block_notes(i); }
        for (std::size_t i=0;i<3;++i) {
            auto box=emotion_bounds(i);
            float left=static_cast<float>(timeline_x(block_start(i)));
            float right=static_cast<float>(timeline_x(block_end(i)));
            require(std::abs(box.X+box.Width/2-(left+right)/2)<0.01,"card not centered between boundaries");
            require(box.X>=left && box.GetRight()<=right,"card outside its block");
            require(slot_at_client_point(window,{static_cast<LONG>(box.X+box.Width/2),
                static_cast<LONG>(box.Y+box.Height/2)})==static_cast<int>(i),"card hit box misaligned");
        }
        {
            arrow_image.reset(new Gdiplus::Image(L"assets\\arrow.png"));
            HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
            HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
            HGDIOBJ old=SelectObject(dc,bitmap);
            draw_background(window,dc,true);
            {
                Gdiplus::Bitmap snapshot(bitmap,nullptr);
                std::unique_ptr<Gdiplus::Bitmap> crop(snapshot.Clone(50,295,900,600,PixelFormat32bppARGB));
                CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                require(crop && crop->Save(L"tests\\card_alignment_preview.png",&png,nullptr)==Gdiplus::Ok,
                    "alignment preview save failed");
            }
            SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
            arrow_image.reset();
        }
        auto point_at=[](double beat,int pitch) {
            return POINT{static_cast<LONG>(timeline_x(beat)),
                static_cast<LONG>(347+(roll_high_pitch-pitch+0.5)*298/(roll_high_pitch-roll_low_pitch+1))};
        };
        auto add=point_at(2.25,69);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(add.x,add.y));
        require(rhythm.events.size()==5 && rhythm.events[2].pitches.back()==69,
            "blank lane did not inherit existing attack");
        auto pcs=ncnl::parse_roll_notes(displayed_progression.chords[1].notes);
        require(std::find(pcs.begin(),pcs.end(),9)!=pcs.end(),"header did not sync added A");
        require(create_midi_note_at_client_point(window,add) && rhythm.events[2].pitches.size()==4,
            "duplicate note created");
        auto gap=point_at(0.65,71);
        require(create_midi_note_at_client_point(window,gap),"rest-area click did not create attack");
        require(rhythm.events.size()==6 && chord_blocks[0].last==2 && chord_blocks[1].first==2 &&
            chord_blocks[1].last==5 && chord_blocks[2].first==5,"insert corrupted group indices");
        require(rhythm.events[1].pitches==std::vector<int>({71}) && rhythm.events[1].start>=0.5 &&
            rhythm.events[1].start+rhythm.events[1].duration<=1,"new attack overlaps neighbours");
        require(rhythm_splits[1] && rhythm_splits[4] && slot_presets==std::vector<int>({0,2,4}),
            "new attack lost split or preset settings");
        require(create_midi_note_at_client_point(window,point_at(5.2,65)),"trailing rest edit failed");
        require(rhythm.events.size()==7 && chord_blocks.back().last==7 && rhythm.length==6,
            "trailing attack changed loop length");
        require(!create_midi_note_at_client_point(window,{80,633}),"keyboard click added grid note");
        require(!create_midi_note_at_client_point(window,{200,320}),"header click added grid note");
        initialize_rhythm(); rhythm_splits.assign(3,true);
        displayed_progression.chords.assign(4,{"",0}); slot_presets.assign(4,2);
        active_slot=0;
        for (std::size_t i=0;i<4;++i) {
            displayed_progression.chords[i]={"C E G",50}; apply_block_notes(i);
        }
        clear_chord_at_client_point(window,{200,533}); // E4, first event.
        require(rhythm.events[0].pitches==std::vector<int>({60,67}),"deletion delayed or removed wrong note");
        require(dissolving_notes.size()==1,"note erase did not create a dissolve");
        require(dissolving_notes[0].bounds.Width>170,"dissolve lost original geometry");
        clear_chord_at_client_point(window,{200,533});
        require(dissolving_notes.size()==1,"hovering deleted note spawned duplicate effects");
        clear_chord_at_client_point(window,{430,320});
        require(dissolving_notes.size()==4 && rhythm.events[1].pitches.empty(),"header erasure missed voices");
        auto born=dissolving_notes[0].born;
        expire_dissolving_notes(born+100);
        require(!dissolving_notes.empty(),"particles expired early");
        expire_dissolving_notes(monotonic_ms()+DISSOLVE_DURATION_MS);
        require(dissolving_notes.empty(),"particles did not expire");
        for (int i=0;i<100;++i) { start_note_dissolve(2,64); }
        require(dissolving_notes.size()==64,"particle budget is unbounded");
        discard_note_effects();
        for (std::size_t i=0;i<4;++i) { apply_block_notes(i); }
        // One running chord plus two deleted voices, sampled at different animation ages.
        displayed_progression.chords[1]={"C E G",50}; apply_block_notes(1);
        clear_chord_at_client_point(window,{200,633});
        clear_chord_at_client_point(window,{430,533});
        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        HGDIOBJ old=SelectObject(dc,bitmap);
        Gdiplus::Bitmap montage(900,1050,PixelFormat32bppARGB);
        Gdiplus::Graphics gallery(&montage);
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        for (int frame=0;frame<3;++frame) {
            draw_background(window,dc,true);
            ULONGLONG now=monotonic_ms();
            for (auto& effect:dissolving_notes) { effect.born=now-(80+frame*250); }
            playback_start_ms=now-1250; playback_loop_ms=2000;
            draw_playback_overlay(dc); draw_dissolve_overlay(dc);
            Gdiplus::Bitmap snapshot(bitmap,nullptr);
            require(gallery.DrawImage(&snapshot,Gdiplus::Rect(0,frame*350,900,350),
                50,295,900,350,Gdiplus::UnitPixel)==Gdiplus::Ok,"frame render failed");
        }
        require(montage.Save(L"tests\\note_effects_preview.png",&png,nullptr)==Gdiplus::Ok,
            "preview save failed");
        for (double scale:{0.6,1.4}) {
            current_scale=scale;
            draw_playback_overlay(dc); draw_dissolve_overlay(dc);
        }
        current_scale=1.0;
        playback_loop_ms=0;
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
        discard_note_effects();
        main_background_dirty=true;
        // Exercise cached animated painting and the idle restoration path.
        InvalidateRect(window,nullptr,FALSE); UpdateWindow(window);
        start_note_dissolve(2,64);
        InvalidateRect(window,nullptr,FALSE); UpdateWindow(window);
        expire_dissolving_notes(monotonic_ms()+DISSOLVE_DURATION_MS);
        InvalidateRect(window,nullptr,FALSE); UpdateWindow(window);
        require(dissolving_notes.empty(),"idle frame retained expired effects");
        std::cout<<"PASS: keyboard hit/release, blank-lane/rest note creation, header sync, split/card alignment; erase/particles/rendering\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    if (window) { DestroyWindow(window); }
    arrow_image.reset();
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    unload_interface_font();
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return result;
}
