#pragma once
#include <windows.h>
#include <algorithm>

namespace ncnl {
// Center owned popups on the application, keeping them on the owner's monitor.
inline POINT centered_window_position(HWND owner,int width,int height) {
    MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
    RECT anchor=monitor.rcWork;
    if (owner && IsWindow(owner) && !IsIconic(owner)) { GetWindowRect(owner,&anchor); }
    LONG x=anchor.left+(anchor.right-anchor.left-width)/2;
    LONG y=anchor.top+(anchor.bottom-anchor.top-height)/2;
    const RECT& work=monitor.rcWork;
    if (width<=work.right-work.left) { x=std::max(work.left,std::min(x,work.right-width)); }
    if (height<=work.bottom-work.top) { y=std::max(work.top,std::min(y,work.bottom-height)); }
    return {x,y};
}
}