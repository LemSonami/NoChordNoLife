#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void clean(const std::wstring& source,const std::wstring& target) {
    Gdiplus::Bitmap image(source.c_str());
    if (image.GetLastStatus()!=Gdiplus::Ok || !image.GetWidth() || !image.GetHeight() ||
        image.GetWidth()>8192 || image.GetHeight()>8192) { throw std::runtime_error("无法读取原图。"); }
    int width=image.GetWidth(),height=image.GetHeight();
    std::vector<std::uint32_t> pixels(width*height,0);
    Gdiplus::Rect rect(0,0,width,height); Gdiplus::BitmapData data={};
    if (image.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) {
        throw std::runtime_error("无法读取像素。");
    }
    int left=width,top=height,right=-1,bottom=-1;
    for (int y=0;y<height;++y) {
        auto row=reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(data.Scan0)+y*data.Stride);
        for (int x=0;x<width;++x) { if ((row[x]>>24)>8) {
            pixels[y*width+x]=row[x]; left=std::min(left,x); right=std::max(right,x);
            top=std::min(top,y); bottom=std::max(bottom,y);
        } }
    }
    image.UnlockBits(&data); if (right<left) { throw std::runtime_error("原图没有可见内容。"); }
    int w=right-left+1,h=bottom-top+1; std::vector<std::uint32_t> cropped(w*h);
    for (int y=0;y<h;++y) { std::copy(pixels.begin()+(y+top)*width+left,pixels.begin()+(y+top)*width+left+w,cropped.begin()+y*w); }
    Gdiplus::Bitmap output(w,h,w*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(cropped.data()));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    if (output.Save(target.c_str(),&png,nullptr)!=Gdiplus::Ok) { throw std::runtime_error("无法保存图片。"); }
    std::cout<<"alpha > 8: "<<w<<"x"<<h<<"\n";
}
}

int main(int argc,char* argv[]) {
    if (argc!=3) { std::cerr<<"用法：plugin_asset_clean 原图目录 输出目录\n"; return 1; }
    std::string from=argv[1],to=argv[2]; std::wstring input(from.begin(),from.end()),output(to.begin(),to.end());
    ULONG_PTR token=0; Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) { return 1; }
    int result=0;
    try {
        if (input==output) { throw std::runtime_error("请使用单独的输出目录并保留原图。"); }
        for (const auto& name:{L"ncnl",L"plugins_tab"}) {
            auto filename=std::wstring(name)+L".png";
            clean(input+L"/"+filename,output+L"/"+filename);
        }
    } catch (const std::exception& error) { std::cerr<<error.what()<<"\n"; result=2; }
    Gdiplus::GdiplusShutdown(token); return result;
}
