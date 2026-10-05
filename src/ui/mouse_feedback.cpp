#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "mouse_feedback.hpp"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include "../resources/embedded_assets.hpp"

namespace ncnl {
namespace {
const UINT BUTTONS[3]={MK_LBUTTON,MK_MBUTTON,MK_RBUTTON};
const BYTE COLORS[3][3]={{162,246,194},{220,229,239},{255,167,177}};
constexpr ULONGLONG PARTICLE_LIFE=800,TRAIL_LIFE=380;
LRESULT CALLBACK overlay_procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) { return HTTRANSPARENT; }
    if (message==WM_MOUSEACTIVATE) { return MA_NOACTIVATE; }
    if (message==WM_ERASEBKGND) { return 1; }
    return DefWindowProcW(window,message,w,l);
}
}

void MouseFeedback::initialize(HWND host,const std::wstring& directory) {
    shutdown(); owner=host;
    const wchar_t* names[4]={L"Link.ani",L"Vertical Resize.ani",L"Unavailable.ani",L"Text Select.ani"};
    for (int i=0;i<4;++i) { cursors[i]=asset_cursor(directory+L"/"+names[i]); }
}
HCURSOR MouseFeedback::cursor(AppCursor kind) const {
    int index=static_cast<int>(kind);
    if (cursors[index]) { return cursors[index]; }
    const LPCWSTR defaults[4]={IDC_HAND,IDC_SIZENS,IDC_NO,IDC_IBEAM};
    return LoadCursorW(nullptr,defaults[index]);
}
void MouseFeedback::shutdown() {
    clear();
    if (overlay) { DestroyWindow(overlay); overlay=nullptr; }
    if (buffer) {
        SelectObject(buffer,old_bitmap); DeleteObject(bitmap); DeleteDC(buffer);
    }
    buffer=nullptr; bitmap=nullptr; pixels=nullptr; buffer_width=buffer_height=0;
    for (auto& value:cursors) { if (value) { DestroyCursor(value); value=nullptr; } }
    owner=nullptr;
}
void MouseFeedback::break_trail() { previous_buttons=0; ++stroke; }
void MouseFeedback::clear() {
    trail.clear(); particles.clear(); break_trail();
    if (overlay) { ShowWindow(overlay,SW_HIDE); }
}
void MouseFeedback::emit(float x,float y,int color,bool burst,float scale,ULONGLONG now) {
    auto random=[this]() {
        random_seed^=random_seed<<13; random_seed^=random_seed>>17; random_seed^=random_seed<<5;
        return (random_seed&65535u)/65535.0f;
    };
    unsigned count=burst ? 36 : 2;
    for (unsigned i=0;i<count;++i) {
        float angle=(i+random())*6.2831853f/count;
        float speed=(burst ? 45+random()*120 : 12+random()*34)*scale;
        particles.push_back({x,y,std::cos(angle)*speed,std::sin(angle)*speed,
            (burst ? 1.0f+random()*1.6f : 0.7f+random()*1.2f)*scale,now,color,i%6==0});
    }
    if (particles.size()>512) { particles.erase(particles.begin(),particles.end()-512); }
}
void MouseFeedback::input(POINT point,UINT buttons,bool click,float scale,ULONGLONG now) {
    if (!owner) { return; }
    RECT client{}; GetClientRect(owner,&client);
    if (!PtInRect(&client,point)) { break_trail(); return; }
    effect_scale=scale;
    if (click) { emit(static_cast<float>(point.x),static_cast<float>(point.y),0,true,scale,now); }
    buttons&=MK_LBUTTON|MK_MBUTTON|MK_RBUTTON;
    if (buttons!=previous_buttons) { ++stroke; }
    std::size_t old_trail_size=trail.size();
    for (int color=0;color<3;++color) {
        if (!(buttons&BUTTONS[color])) { continue; }
        bool connected=(previous_buttons&BUTTONS[color])!=0;
        float dx=static_cast<float>(point.x-previous.x),dy=static_cast<float>(point.y-previous.y);
        float distance=std::sqrt(dx*dx+dy*dy);
        if (connected && distance<3*scale) { continue; }
        unsigned steps=connected ? static_cast<unsigned>(std::min(32.0f,std::max(1.0f,distance/(5*scale)))) : 1;
        for (unsigned step=1;step<=steps;++step) {
            float fraction=static_cast<float>(step)/steps;
            float x=connected ? previous.x+dx*fraction : static_cast<float>(point.x);
            float y=connected ? previous.y+dy*fraction : static_cast<float>(point.y);
            trail.push_back({x,y,now,color,stroke}); emit(x,y,color,false,scale,now);
        }
    }
    if (trail.size()>192) { trail.erase(trail.begin(),trail.end()-192); }
    if (trail.size()!=old_trail_size || buttons!=previous_buttons) { previous=point; }
    previous_buttons=buttons;
}
void MouseFeedback::expire(ULONGLONG now) {
    trail.erase(std::remove_if(trail.begin(),trail.end(),[now](const TrailPoint& p){
        return now-p.born>=TRAIL_LIFE; }),trail.end());
    particles.erase(std::remove_if(particles.begin(),particles.end(),[now](const Particle& p){
        return now-p.born>=PARTICLE_LIFE; }),particles.end());
    if (!active() && overlay) { ShowWindow(overlay,SW_HIDE); }
}
bool MouseFeedback::ensure_buffer(int width,int height) {
    if (buffer && buffer_width>=width && buffer_height>=height) { return true; }
    if (buffer) { SelectObject(buffer,old_bitmap); DeleteObject(bitmap); DeleteDC(buffer); }
    buffer=nullptr; bitmap=nullptr;
    buffer_width=(width+63)/64*64; buffer_height=(height+63)/64*64;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=buffer_width; info.bmiHeader.biHeight=-buffer_height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    buffer=CreateCompatibleDC(nullptr);
    bitmap=CreateDIBSection(buffer,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!buffer || !bitmap) {
        if (bitmap) { DeleteObject(bitmap); } if (buffer) { DeleteDC(buffer); }
        bitmap=nullptr; buffer=nullptr; return false;
    }
    old_bitmap=SelectObject(buffer,bitmap); return true;
}
void MouseFeedback::render(ULONGLONG now) {
    if (!active() || !owner || !IsWindowVisible(owner) || IsIconic(owner)) {
        if (overlay) { ShowWindow(overlay,SW_HIDE); } return;
    }
    RECT client{}; GetClientRect(owner,&client);
    float left=static_cast<float>(client.right),top=static_cast<float>(client.bottom),right=0,bottom=0;
    auto include=[&](float x,float y) { left=std::min(left,x); top=std::min(top,y);
        right=std::max(right,x); bottom=std::max(bottom,y); };
    for (const auto& p:trail) { include(p.x,p.y); }
    for (const auto& p:particles) {
        float age=static_cast<float>(now-p.born)/PARTICLE_LIFE;
        float drift=(1-std::pow(1-age,3.0f))*0.45f;
        include(p.x+p.vx*drift,p.y+p.vy*drift);
    }
    RECT bounds={std::max(0,static_cast<int>(std::floor(left-16*effect_scale))),
        std::max(0,static_cast<int>(std::floor(top-16*effect_scale))),
        std::min(client.right,static_cast<LONG>(std::ceil(right+16*effect_scale))),
        std::min(client.bottom,static_cast<LONG>(std::ceil(bottom+16*effect_scale)))};
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if (width<=0 || height<=0 || !ensure_buffer(width,height)) { return; }
    if (!overlay) {
        WNDCLASSW type{}; type.lpfnWndProc=overlay_procedure; type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"NoChordNoLifeMouseEffects"; RegisterClassW(&type);
        overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            type.lpszClassName,L"",WS_POPUP,0,0,1,1,owner,nullptr,type.hInstance,nullptr);
        if (!overlay) { return; }
    }
    std::fill_n(static_cast<DWORD*>(pixels),buffer_width*height,0u);
    {
        Gdiplus::Bitmap surface(buffer_width,height,buffer_width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics graphics(&surface); graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetClip(Gdiplus::Rect(0,0,width,height));
        graphics.TranslateTransform(static_cast<float>(-bounds.left),static_cast<float>(-bounds.top));
        const TrailPoint* previous_point[3]={nullptr,nullptr,nullptr};
        for (const auto& p:trail) {
            float fade=1-static_cast<float>(now-p.born)/TRAIL_LIFE;
            const BYTE* color=COLORS[p.color]; auto previous=previous_point[p.color];
            if (previous && previous->stroke==p.stroke &&
                std::hypot(p.x-previous->x,p.y-previous->y)<90*effect_scale) {
                Gdiplus::Pen halo(Gdiplus::Color(static_cast<BYTE>(32*fade),color[0],color[1],color[2]),7*effect_scale);
                Gdiplus::Pen core(Gdiplus::Color(static_cast<BYTE>(160*fade),color[0],color[1],color[2]),1.6f*effect_scale);
                halo.SetStartCap(Gdiplus::LineCapRound); halo.SetEndCap(Gdiplus::LineCapRound);
                core.SetStartCap(Gdiplus::LineCapRound); core.SetEndCap(Gdiplus::LineCapRound);
                graphics.DrawLine(&halo,previous->x,previous->y,p.x,p.y);
                graphics.DrawLine(&core,previous->x,previous->y,p.x,p.y);
            }
            previous_point[p.color]=&p;
        }
        for (const auto& p:particles) {
            float age=static_cast<float>(now-p.born)/PARTICLE_LIFE;
            float fade=(1-age)*(1-age),drift=(1-std::pow(1-age,3.0f))*0.45f;
            float x=p.x+p.vx*drift,y=p.y+p.vy*drift,r=p.radius*(1-age*0.5f);
            const BYTE* color=COLORS[p.color];
            Gdiplus::SolidBrush halo(Gdiplus::Color(static_cast<BYTE>(38*fade),color[0],color[1],color[2]));
            Gdiplus::SolidBrush core(Gdiplus::Color(static_cast<BYTE>(220*fade),color[0],color[1],color[2]));
            graphics.FillEllipse(&halo,x-r*3,y-r*3,r*6,r*6); graphics.FillEllipse(&core,x-r,y-r,r*2,r*2);
            if (p.star) {
                Gdiplus::Pen ray(Gdiplus::Color(static_cast<BYTE>(120*fade),color[0],color[1],color[2]),0.8f*effect_scale);
                graphics.DrawLine(&ray,x-r*4,y,x+r*4,y); graphics.DrawLine(&ray,x,y-r*4,x,y+r*4);
            }
        }
    }
    POINT position={bounds.left,bounds.top}; ClientToScreen(owner,&position);
    POINT origin={0,0}; SIZE size={width,height}; BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(overlay,nullptr,&position,&size,buffer,&origin,0,&blend,ULW_ALPHA);
    ShowWindow(overlay,SW_SHOWNOACTIVATE);
}
}
