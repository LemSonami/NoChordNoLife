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
        require(chord_blocks.size()==3 && !rhythm_splits[0] && chord_blocks[0].last==2,"arrow did not merge");
        require(rhythm.events[1].pitches==rhythm.events[0].pitches && slot_presets[0]==0,
            "arrow merge failed to use left settings");
        clear_arrow(1);
        require(chord_blocks.size()==3,"hovering erased arrow cleared another block");
        clear_arrow(3); clear_arrow(2);
        require(chord_blocks.size()==1 && rhythm.events.size()==4 && slot_presets[0]==0,
            "continuous arrow clearing corrupted rhythm");
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

        HDC reference=GetDC(window),dc=CreateCompatibleDC(reference);
        HBITMAP bitmap=CreateCompatibleBitmap(reference,1000,1000);
        HGDIOBJ old=SelectObject(dc,bitmap);
        draw_background(window,dc,true);
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
        start=monotonic_ms(); int frames=0;
        while (monotonic_ms()-start<1000) {
            if (PeekMessageW(&message,window,WM_ANIMATION_FRAME,WM_ANIMATION_FRAME,PM_REMOVE)) {
                ++frames; DispatchMessageW(&message);
            } else { Sleep(1); }
        }
        double fps=frames*1000.0/(monotonic_ms()-start);
        playback_loop_ms=0; update_animation_clock();
        require(frames>0 && !animation_clock && !animation_period_active,"animation clock leaked or did not run");
        std::cout<<"Hidden-window animation frame dispatch: "<<fps<<" FPS (not display presentation FPS)\n";
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,reference);
        std::cout<<"PASS: strict randomized quality range, fixed constraints/failure, arrow hover merge, radial/distributed particles, clock coalescing/cleanup\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; result=1; }
    playback_loop_ms=0; discard_note_effects(); stop_animation_clock();
    if (window) { DestroyWindow(window); }
    for (HFONT font:{title_font,normal_font,card_font,hint_font}) { if (font) { DeleteObject(font); } }
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return result;
}
