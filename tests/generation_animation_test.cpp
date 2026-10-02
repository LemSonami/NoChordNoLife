#include "../chord_gui.cpp"
#include <iostream>
#include <limits>

void require(bool condition,const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
int main() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token,&input,nullptr)!=Gdiplus::Ok) { return 1; }
    WNDCLASSW type{}; type.lpfnWndProc=window_procedure;
    type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=L"GenerationAnimationRegression";
    RegisterClassW(&type);
    RECT size={0,0,1000,1000}; AdjustWindowRect(&size,WS_OVERLAPPEDWINDOW,FALSE);
    HWND window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
        size.right-size.left,size.bottom-size.top,nullptr,nullptr,type.hInstance,nullptr);
    int result=0;
    try {
        require(window!=nullptr,"window creation failed");
        require(!ncnl::quality_matches_generation_range(65) && !ncnl::quality_matches_generation_range(90),
            "strict endpoints were accepted");
        require(ncnl::quality_matches_generation_range(65.001) && ncnl::quality_matches_generation_range(89.999),
            "valid quality rejected");
        require(!ncnl::quality_matches_generation_range(std::numeric_limits<double>::quiet_NaN()),"NaN accepted");
        std::vector<ncnl::ChordConstraint> constraints(4);
        for (auto& constraint:constraints) { constraint.emotion_preset=-1; }
        for (int iteration=0;iteration<6;++iteration) {
            if (iteration==3) { constraints[1].fixed_notes="C E G"; }
            auto generated=ncnl::generate_progression("C Ionian",constraints);
            require(generated.quality_score>65 && generated.quality_score<90,"generated score outside range");
            require(generated.chords.size()==4,"generation changed block count");
            if (iteration>=3) { require(generated.chords[1].notes=="C E G","fixed chord changed"); }
        }
        for (auto& constraint:constraints) { constraint.fixed_notes="C E G"; }
        bool rejected=false;
        try { ncnl::generate_progression("C Ionian",constraints); }
        catch (const std::runtime_error&) { rejected=true; }
        require(rejected,"unqualified fixed progression was silently returned");
        int modal_chords=0,dyads=0,total_chords=0;
        constraints.assign(4,{-1,""});
        for (int iteration=0;iteration<24;++iteration) {
            std::string mode_name=iteration<12 ? "C Ionian" : "D Dorian";
            auto mode=ncnl::parse_mode(mode_name);
            auto generated=ncnl::generate_progression(mode_name,constraints);
            require(ncnl::quality_matches_generation_range(generated.quality_score),"preference changed quality bounds");
            for (const auto& chord:generated.chords) {
                auto notes=ncnl::parse_chord(chord.notes);
                bool in_mode=true;
                for (int note:notes) {
                    int interval=(note-mode.tonic+12)%12;
                    in_mode=in_mode && std::find(mode.intervals.begin(),mode.intervals.end(),interval)!=mode.intervals.end();
                }
                modal_chords+=in_mode; dyads+=notes.size()==2; ++total_chords;
            }
        }
        std::cout<<"Weighted generation: "<<modal_chords<<"/"<<total_chords
            <<" modal chords, "<<dyads<<" dyads\n";
        require(modal_chords>total_chords*0.6 && dyads<total_chords*0.3,
            "modal/full-chord preference ineffective");
        constraints[1].fixed_notes="C E"; constraints[1].emotion_preset=0;
        auto fixed_dyad=ncnl::generate_progression("C Ionian",constraints);
        require(fixed_dyad.chords[1].notes=="C E","explicit dyad replaced by soft preference");
        for (std::size_t i=0;i<4;++i) {
            displayed_progression.chords[i]={i%2?"D F A":"C E G",50};
            slot_presets[i]=static_cast<int>(i); apply_block_notes(i);
        }
        auto clear_arrow=[window](std::size_t right) {
            auto arrow=arrow_bounds(right);
            SendMessageW(window,WM_CLEAR_CHORD_HOVER,0,
                MAKELPARAM(static_cast<int>(arrow.X+arrow.Width/2),static_cast<int>(arrow.Y+arrow.Height/2)));
        };
        clear_arrow(1);
        require(!dissolving_notes.empty() && dissolving_notes.back().arrow,
            "arrow erase did not emit particles");
        require(animation_effect_bounds().bottom>=735,"arrow animation not included in repaint bounds");
        require(chord_blocks.size()==3 && !rhythm_splits[0] && chord_blocks[0].last==2,"arrow did not merge");
        require(rhythm.events[1].pitches==rhythm.events[0].pitches && slot_presets[0]==0,
            "arrow merge failed to use left settings");
        clear_arrow(1);
        require(chord_blocks.size()==3,"hovering erased arrow cleared another block");
        require(dissolving_notes.size()==1,"empty arrow hover emitted duplicate effect");
        clear_arrow(3); clear_arrow(2);
        require(chord_blocks.size()==1 && rhythm.events.size()==4 && slot_presets[0]==0,
            "continuous arrow clearing corrupted rhythm");
        require(dissolving_notes.size()==3,"arrow sweep did not emit one effect per boundary");
        expire_dissolving_notes(monotonic_ms()+DISSOLVE_DURATION_MS);
        require(dissolving_notes.empty(),"arrow effect did not expire");
        Gdiplus::RectF box(10,20,100,30);
        auto right=radial_particle_position(box,0,20),down=radial_particle_position(box,1.5707963f,20);
        auto left=radial_particle_position(box,3.14159265f,20),up=radial_particle_position(box,4.712389f,20);
        require(right.X>60 && left.X<60 && down.Y>35 && up.Y<35,"particles do not radiate in all directions");
        float min_x=box.X+box.Width,max_x=box.X;
        for (unsigned spark=0;spark<18;++spark) {
            auto origin=playback_particle_position(box,123,spark,18,0,0);
            require(origin.X>=box.X && origin.X<=box.X+box.Width &&
                origin.Y>=box.Y && origin.Y<=box.Y+box.Height,"playback emission outside note");
            min_x=std::min(min_x,origin.X); max_x=std::max(max_x,origin.X);
            auto drift=playback_particle_position(box,123,spark,18,1.5707963f,20);
            require(std::abs(drift.X-origin.X)<0.001f && drift.Y>origin.Y,
                "distributed playback particle lost radial motion");
        }
        require(min_x<box.X+box.Width*0.06f && max_x>box.X+box.Width*0.94f,
            "playback emission does not span the whole note bar");

        rhythm_splits.assign(rhythm.events.size()-1,true); rebuild_chord_blocks();
        slot_presets={0,1,2,3};
        load_background(L"assets\\bg.png");
        for (std::size_t i=0;i<5;++i) {
            std::wostringstream path; path<<L"assets\\chord_emotion\\"<<i+1;
            emotion_images[i]=load_emotion_icon(path.str());
            initialize_emotion_animation(i,monotonic_ms());
            require(!emotion_animations[i].frames.empty(),"GIF frame cache missing");
            require(emotion_animations[i].frames.size()==emotion_animations[i].delays.size(),
                "GIF frame cache incomplete");
        }

        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        HGDIOBJ old=SelectObject(dc,bitmap);
        arrow_image=load_png(L"assets\\arrow.png");
        start_arrow_dissolve(Gdiplus::RectF(518,658,42,42));
        dissolving_notes.back().born-=200;
        draw_background(window,dc,true); draw_dissolve_overlay(dc);
        {
            Gdiplus::Bitmap snapshot(bitmap,nullptr);
            std::unique_ptr<Gdiplus::Bitmap> crop(snapshot.Clone(490,640,100,95,PixelFormat32bppARGB));
            CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
            require(crop->Save(L"tests\\arrow_effects_preview.png",&png,nullptr)==Gdiplus::Ok,
                "arrow effect preview failed");
        }
        discard_note_effects();
        draw_background(window,dc,true);
        ULONGLONG full_start=monotonic_ms();
        for (int i=0;i<5;++i) { draw_background(window,dc,true); }
        std::cout<<"Full-scene render cost avoided on GIF ticks: "<<(monotonic_ms()-full_start)/5.0<<" ms\n";
        ensure_main_background_buffer(window,reference);
        playback_start_ms=monotonic_ms(); playback_loop_ms=2000;
        ULONGLONG start=monotonic_ms();
        for (int i=0;i<120;++i) {
            BitBlt(dc,132,347,818,298,main_background_dc,132,347,SRCCOPY);
            draw_playback_overlay(dc);
        }
        double render_ms=(monotonic_ms()-start)/120.0;
        std::cout<<"1000px cached animation render average: "<<render_ms<<" ms/frame\n";
        update_animation_clock();
        Sleep(60); stop_animation_clock();
        MSG message{}; int backlog=0;
        while (PeekMessageW(&message,window,WM_ANIMATION_FRAME,WM_ANIMATION_FRAME,PM_REMOVE)) { ++backlog; }
        require(backlog<=1,"animation refresh queue accumulated frames");
        update_animation_clock();
        update_emotion_animation_timer(true);
        ShowWindow(window,SW_SHOWNOACTIVATE);
        start=monotonic_ms(); int frames=0,gif_ticks=0;
        while (monotonic_ms()-start<2000) {
            if (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message==WM_ANIMATION_FRAME) {
                    ++frames;
                    if (frames%2==0) {
                        POINT pointer={500+static_cast<LONG>(80*std::sin(frames*0.06)),
                            210+static_cast<LONG>(30*std::cos(frames*0.08))};
                        mouse_feedback.input(pointer,MK_LBUTTON,false,1,monotonic_ms());
                    }
                }
                DispatchMessageW(&message);
                if (message.message==WM_TIMER && message.wParam==EMOTION_GIF_TIMER) {
                    ++gif_ticks;
                    require(!main_background_dirty,"GIF tick invalidated full-scene cache");
                    // Hidden child windows do not present; explicitly exercise all five button draws.
                    for (int i=0;i<5;++i) {
                        DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.CtlID=ID_PRESET_BASE+i;
                        item.hwndItem=preset_buttons[i]; item.hDC=dc; item.rcItem={0,0,115,115};
                        draw_square_button(&item);
                    }
                    RECT cards={132,SLOT_Y,951,920};
                    BitBlt(dc,cards.left,cards.top,cards.right-cards.left,cards.bottom-cards.top,
                        main_background_dc,cards.left,cards.top,SRCCOPY);
                    draw_emotion_overlay(dc,cards);
                }
            } else { Sleep(1); }
        }
        double fps=frames*1000.0/(monotonic_ms()-start);
        mouse_feedback.clear();
        playback_loop_ms=0; update_animation_clock();
        require(frames>0 && gif_ticks>0 && !animation_clock && !animation_period_active,"animation clock leaked or did not run");
        std::cout<<"Playback + five animated GIFs + four cards + mouse trail: "<<fps
            <<" FPS frame dispatch, "<<gif_ticks<<" GIF ticks (not display presentation FPS)\n";
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
        std::cout<<"PASS: strict randomized quality range, fixed constraints/failure, arrow hover merge, radial/distributed particles, clock coalescing/cleanup\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    playback_loop_ms=0; discard_note_effects(); stop_animation_clock();
    if (window) { DestroyWindow(window); }
    for (auto& image:emotion_images) { image.reset(); }
    for (auto& animation:emotion_animations) { animation.frames.clear(); }
    background_image.reset();
    arrow_image.reset();
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return result;
}
