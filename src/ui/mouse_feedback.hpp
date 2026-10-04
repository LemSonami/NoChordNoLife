#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>

namespace ncnl {
enum class AppCursor { link,vertical,unavailable,text };


class MouseFeedback {
public:
    void initialize(HWND owner,const std::wstring& asset_directory);
    void shutdown();
    HCURSOR cursor(AppCursor kind) const;
    void input(POINT point,UINT buttons,bool left_click,float scale,ULONGLONG now);
    void break_trail();
    void expire(ULONGLONG now);
    bool active() const { return !particles.empty() || !trail.empty(); }
    void clear();
    void render(ULONGLONG now);
    std::size_t particle_count() const { return particles.size(); }
    std::size_t trail_count() const { return trail.size(); }
    HWND overlay_window() const { return overlay; }
private:
    friend struct MouseFeedbackTestAccess;
    friend struct MouseFeedbackTestAccess;
    struct TrailPoint { float x,y; ULONGLONG born; int color; unsigned stroke; };
    struct Particle { float x,y,vx,vy,radius; ULONGLONG born; int color; bool star; };
    void emit(float x,float y,int color,bool burst,float scale,ULONGLONG now);
    bool ensure_buffer(int width,int height);
    HWND owner=nullptr,overlay=nullptr;
    HDC buffer=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ old_bitmap=nullptr;
    void* pixels=nullptr;
    int buffer_width=0,buffer_height=0;
    std::array<HCURSOR,4> cursors={{nullptr,nullptr,nullptr,nullptr}};
    std::vector<TrailPoint> trail;
    std::vector<Particle> particles;
    POINT previous={0,0};
    UINT previous_buttons=0;
    float effect_scale=1;
    unsigned random_seed=7163;
    unsigned stroke=0;
};
}