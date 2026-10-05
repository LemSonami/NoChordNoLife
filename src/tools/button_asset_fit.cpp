#define UNICODE
#define _UNICODE
#include "../ui/button_artwork.hpp"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct Box { int x0,y0,x1,y1; };
struct Raster {
    int w,h;
    std::vector<std::uint32_t> pixels;
    std::vector<int> origins;
    std::uint32_t at(int x,int y) const { return pixels[y*w+x]; }
};
struct Protected { Box box; int kind; };
int channel(std::uint32_t p,int shift) { return (p>>shift)&255; }
int difference(std::uint32_t a,std::uint32_t b) {
    return std::abs(channel(a,16)-channel(b,16))+std::abs(channel(a,8)-channel(b,8))+
        std::abs(channel(a,0)-channel(b,0))+std::abs(channel(a,24)-channel(b,24));
}
int gcd(int a,int b) { while (b) { int r=a%b; a=b; b=r; } return a; }
Raster read(const std::wstring& path) {
    Gdiplus::Bitmap image(path.c_str());
    if (image.GetLastStatus()!=Gdiplus::Ok || image.GetWidth()>8192 || image.GetHeight()>8192) {
        throw std::runtime_error("无法解码源PNG…");
    }
    Raster full{static_cast<int>(image.GetWidth()),static_cast<int>(image.GetHeight()),{}, {}};
    full.pixels.resize(full.w*full.h);
    Gdiplus::Rect rectangle(0,0,full.w,full.h); Gdiplus::BitmapData data={};
    if (image.LockBits(&rectangle,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) {
        throw std::runtime_error("无法读取源像素…");
    }
    int x0=full.w,y0=full.h,x1=-1,y1=-1;
    for (int y=0;y<full.h;++y) {
        auto row=reinterpret_cast<const std::uint32_t*>(static_cast<const BYTE*>(data.Scan0)+y*data.Stride);
        for (int x=0;x<full.w;++x) {
            auto pixel=channel(row[x],24)>8 ? row[x] : 0;
            full.pixels[y*full.w+x]=pixel;
            if (pixel) { x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x); y1=std::max(y1,y); }
        }
    }
    image.UnlockBits(&data);
    if (x1<x0) { throw std::runtime_error("？！空空！？"); }
    Raster crop{x1-x0+1,y1-y0+1,{}, {}}; crop.pixels.resize(crop.w*crop.h);
    for (int y=0;y<crop.h;++y) { for (int x=0;x<crop.w;++x) { crop.pixels[y*crop.w+x]=full.at(x+x0,y+y0); } }
    crop.origins.resize(crop.pixels.size());
    for (std::size_t i=0;i<crop.origins.size();++i) { crop.origins[i]=static_cast<int>(i); }
    return crop;
}
std::vector<BYTE> foreground_mask(const Raster& r) {
    std::vector<BYTE> mask(r.w*r.h,0);
    for (int i=0;i<r.w*r.h;++i) {
        auto p=r.pixels[i]; int red=channel(p,16),green=channel(p,8),blue=channel(p,0);
        if (channel(p,24)>128 && (red*3+green*6+blue)/10<112 && green<160 && blue<140) { mask[i]=1; }
    }
    std::vector<int> component;
    for (int start=0;start<r.w*r.h;++start) {
        if (mask[start]!=1) { continue; }
        component.clear(); component.push_back(start); mask[start]=2;
        int x0=r.w,y0=r.h,x1=-1,y1=-1;
        for (std::size_t next=0;next<component.size();++next) {
            int index=component[next],x=index%r.w,y=index/r.w;
            x0=std::min(x0,x); x1=std::max(x1,x); y0=std::min(y0,y); y1=std::max(y1,y);
            for (int dy=-1;dy<=1;++dy) { for (int dx=-1;dx<=1;++dx) {
                int px=x+dx,py=y+dy;
                if (px<0 || py<0 || px>=r.w || py>=r.h) { continue; }
                int neighbor=py*r.w+px;
                if (mask[neighbor]==1) { mask[neighbor]=2; component.push_back(neighbor); }
            } }
        }
        bool frame=x1-x0>r.w*0.85 || y1-y0>r.h*0.93;
        for (int index:component) { mask[index]=frame ? 2 : 3; }
    }
    return mask;
}
Box ink_bounds(const Raster& r,const std::vector<BYTE>& mask,float left,float right,int margin) {
    Box box{r.w,r.h,-1,-1};
    std::vector<int> row_left(r.h,r.w),row_right(r.h,-1),column_top(r.w,r.h),column_bottom(r.w,-1);
    for (int y=0;y<r.h;++y) { for (int x=0;x<r.w;++x) { if (channel(r.at(x,y),24)>8) {
        row_left[y]=std::min(row_left[y],x); row_right[y]=std::max(row_right[y],x);
        column_top[x]=std::min(column_top[x],y); column_bottom[x]=std::max(column_bottom[x],y);
    } } }
    int rim=static_cast<int>(r.h*0.06f);
    for (int y=static_cast<int>(r.h*0.13f);y<static_cast<int>(r.h*0.90f);++y) {
        for (int x=static_cast<int>(r.w*left);x<static_cast<int>(r.w*right);++x) {
            if (x<=row_left[y]+rim || x>=row_right[y]-rim ||
                y<=column_top[x]+rim || y>=column_bottom[x]-rim) { continue; }
            if (mask[y*r.w+x]==3) {
                box.x0=std::min(box.x0,x); box.x1=std::max(box.x1,x);
                box.y0=std::min(box.y0,y); box.y1=std::max(box.y1,y);
            }
        }
    }
    if (box.x1<box.x0) { throw std::runtime_error("？！空空！？"); }
    box.x0=std::max(1,box.x0-margin); box.y0=std::max(1,box.y0-margin);
    box.x1=std::min(r.w-2,box.x1+margin); box.y1=std::min(r.h-2,box.y1+margin);
    return box;
}
std::vector<Protected> foreground(const Raster& r,ncnl::ButtonArt id) {
    float left=0.26f,right=0.74f;
    if (id==ncnl::ButtonArt::RadarTab) { left=0.14f; right=0.86f; }
    if (id==ncnl::ButtonArt::Confirm) { left=0.22f; right=0.80f; }
    if (id==ncnl::ButtonArt::Save) { left=0.15f; right=0.86f; }
    if (id==ncnl::ButtonArt::Phrygian) { left=0.21f; right=0.79f; }
    if (id==ncnl::ButtonArt::Mixolydian) { left=0.19f; right=0.82f; }
    std::vector<Protected> result;
    auto mask=foreground_mask(r);
    result.push_back({ink_bounds(r,mask,left,right,14),0});
    result.push_back({ink_bounds(r,mask,0.04f,std::max(0.10f,left-0.02f),20),1});
    result.push_back({ink_bounds(r,mask,std::min(0.90f,right+0.02f),0.96f,20),2});
    return result;
}
void transpose(Raster& r,std::vector<Protected>& boxes) {
    Raster next{r.h,r.w,{}, {}}; next.pixels.resize(r.pixels.size());
    next.origins.resize(r.origins.size());
    for (int y=0;y<r.h;++y) { for (int x=0;x<r.w;++x) {
        next.pixels[x*next.w+y]=r.at(x,y); next.origins[x*next.w+y]=r.origins[y*r.w+x];
    } }
    r=std::move(next);
    for (auto& p:boxes) { auto b=p.box; p.box={b.y0,b.x0,b.y1,b.x1}; }
}
void insert(Raster& r,std::vector<Protected>& boxes,int count,bool before,bool horizontal) {
    if (!count) { return; }
    const double infinity=1e30;
    std::vector<bool> preceding;
    const auto word=boxes[0].box;
    for (const auto& p:boxes) {
        bool side=before;
        if (horizontal && p.kind==1) { side=before && p.box.x1>=word.x0-4; }
        if (horizontal && p.kind==2) { side=before || p.box.x0>word.x1+4; }
        preceding.push_back(side);
    }
    std::vector<double> previous(r.w,infinity),current(r.w,infinity);
    std::vector<signed char> directions(r.w*r.h,0);
    auto face=r.at(r.w/2,std::max(0,word.y0/2));
    for (int y=0;y<r.h;++y) {
        int low=1,high=r.w-2;
        if (before) { high=std::min(high,r.w/2); } else { low=std::max(low,r.w/2); }
        if (!horizontal && (y==0 || y==r.h-1)) {
            low=std::max(low,r.w/3); high=std::min(high,r.w*2/3);
        }
        for (std::size_t i=0;i<boxes.size();++i) {
            const auto b=boxes[i].box;
            if (y<b.y0 || y>b.y1) { continue; }
            if (preceding[i]) { high=std::min(high,b.x0-1); }
            else { low=std::max(low,b.x1+1); }
        }
        std::fill(current.begin(),current.end(),infinity);
        for (int x=low;x<=high;++x) {
            auto p=r.at(x,y),q=r.at(x+1,y);
            double energy=difference(p,q)*3+difference(p,face)*0.15;
            if (y>0) { energy+=difference(p,r.at(x,y-1)); }
            if (y+1<r.h) { energy+=difference(p,r.at(x,y+1)); }
            if (channel(p,24)<240 || channel(q,24)<240) { energy+=10000; }
            double best=y ? previous[x] : 0; int offset=0;
            for (int step=-4;y && step<=4;++step) {
                if (x+step<0 || x+step>=r.w) { continue; }
                double candidate=previous[x+step]+std::abs(step)*0.5;
                if (candidate<best) { best=candidate; offset=step; }
            }
            current[x]=best+energy; directions[y*r.w+x]=static_cast<signed char>(offset);
        }
        previous.swap(current);
    }
    auto best=std::min_element(previous.begin(),previous.end());
    if (*best>=infinity/2) { throw std::runtime_error("？！无背景缝隙！？"); }
    std::vector<int> seam(r.h); int x=static_cast<int>(best-previous.begin());
    for (int y=r.h-1;y>=0;--y) { seam[y]=x; x+=directions[y*r.w+x]; }
    Raster next{r.w+count,r.h,{}, {}}; next.pixels.resize(next.w*next.h);
    next.origins.resize(next.pixels.size());
    for (int y=0;y<r.h;++y) {
        int split=seam[y];
        for (int column=0;column<next.w;++column) {
            int old=column<=split ? column : column<=split+count ? split : column-count;
            next.pixels[y*next.w+column]=r.at(old,y); next.origins[y*next.w+column]=r.origins[y*r.w+old];
        }
    }
    r=std::move(next);
    for (std::size_t i=0;i<boxes.size();++i) { if (preceding[i]) { boxes[i].box.x0+=count; boxes[i].box.x1+=count; } }
}
bool contains(Box b,int x,int y) { return x>=b.x0 && x<=b.x1 && y>=b.y0 && y<=b.y1; }
std::vector<BYTE> preserved_pixels(const Raster& source,const std::vector<Protected>& boxes) {
    auto mask=foreground_mask(source);
    std::vector<BYTE> colour(source.w*source.h,0);
    for (int i=0;i<source.w*source.h;++i) {
        auto pixel=source.pixels[i]; int red=channel(pixel,16),green=channel(pixel,8),blue=channel(pixel,0);
        bool leaf=green>red+8 && blue<105 && red<165;
        bool petal=red>210 && green>235 && blue>200;
        bool pollen=red>190 && green>130 && blue<red*0.65;
        if (channel(pixel,24)>128) { colour[i]=(leaf ? 1 : 0) | (petal ? 2 : 0) | (pollen ? 4 : 0); }
    }
    std::vector<BYTE> coloured(source.w*source.h,0);
    std::vector<int> component;
    for (int category=1;category<=4;category*=2) {
      std::vector<BYTE> visited(source.w*source.h,0);
      for (int start=0;start<source.w*source.h;++start) {
        if (!(colour[start]&category) || visited[start]) { continue; }
        component.clear(); component.push_back(start); visited[start]=1;
        int x0=source.w,y0=source.h,x1=-1,y1=-1;
        for (std::size_t next=0;next<component.size();++next) {
            int index=component[next],x=index%source.w,y=index/source.w;
            x0=std::min(x0,x); x1=std::max(x1,x); y0=std::min(y0,y); y1=std::max(y1,y);
            for (int dy=-1;dy<=1;++dy) { for (int dx=-1;dx<=1;++dx) {
                int px=x+dx,py=y+dy;
                if (px<0 || py<0 || px>=source.w || py>=source.h) { continue; }
                int neighbor=py*source.w+px;
                if ((colour[neighbor]&category) && !visited[neighbor]) { visited[neighbor]=1; component.push_back(neighbor); }
            } }
        }
        bool backdrop=x1-x0>source.w*0.75 || y1-y0>source.h*0.90;
        if (!backdrop) { for (int index:component) { coloured[index]=1; } }
      }
    }
    std::vector<BYTE> keep(source.w*source.h,0);
    for (int y=0;y<source.h;++y) { for (int x=0;x<source.w;++x) {
        int index=y*source.w+x;
        if (contains(boxes[0].box,x,y)) { keep[index]=mask[index]==3; continue; }
        if (!contains(boxes[1].box,x,y) && !contains(boxes[2].box,x,y)) { continue; }
        if (mask[index]==3 || coloured[index]) { keep[index]=1; }
    } }
    for (int region=1;region<=2;++region) {
        Box b=boxes[region].box; std::vector<BYTE> visited(source.pixels.size(),0);
        for (int y=b.y0;y<=b.y1;++y) { for (int x=b.x0;x<=b.x1;++x) {
            int start=y*source.w+x;
            if (keep[start] || visited[start]) { continue; }
            component.clear(); component.push_back(start); visited[start]=1; bool exterior=false;
            for (std::size_t next=0;next<component.size();++next) {
                int index=component[next],px=index%source.w,py=index/source.w;
                if (px==b.x0 || px==b.x1 || py==b.y0 || py==b.y1) { exterior=true; }
                const int moves[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& move:moves) {
                    int nx=px+move[0],ny=py+move[1];
                    if (!contains(b,nx,ny)) { continue; }
                    int neighbor=ny*source.w+nx;
                    if (!keep[neighbor] && !visited[neighbor]) { visited[neighbor]=1; component.push_back(neighbor); }
                }
            }
            if (!exterior) { for (int index:component) { keep[index]=1; } }
        } }
    }
    auto expanded=keep;
    for (int y=1;y<source.h-1;++y) { for (int x=1;x<source.w-1;++x) { if (keep[y*source.w+x]) {
        for (int dy=-1;dy<=1;++dy) { for (int dx=-1;dx<=1;++dx) { expanded[(y+dy)*source.w+x+dx]=1; } }
    } } }
    return expanded;
}
void repair_frame(Raster& r,const Raster& source,const std::vector<BYTE>& keep) {
    int rim=static_cast<int>(source.h*0.14f);
    std::vector<int> left(source.h,source.w),right(source.h,-1),top(source.w,source.h),bottom(source.w,-1);
    for (int y=0;y<source.h;++y) { for (int x=0;x<source.w;++x) { if (channel(source.at(x,y),24)>8) {
        left[y]=std::min(left[y],x); right[y]=std::max(right[y],x);
        top[x]=std::min(top[x],y); bottom[x]=std::max(bottom[x],y);
    } } }
    std::vector<int> depth(source.w*source.h);
    std::vector<unsigned> histogram(4096,0);
    for (int y=0;y<source.h;++y) { for (int x=0;x<source.w;++x) {
        int index=y*source.w+x;
        depth[index]=std::min(std::min(x-left[y],right[y]-x),std::min(y-top[x],bottom[x]-y));
        if (depth[index]>=rim && !keep[index] && channel(source.at(x,y),24)>240) {
            auto p=source.at(x,y);
            ++histogram[(channel(p,16)/16)*256+(channel(p,8)/16)*16+channel(p,0)/16];
        }
    } }
    int dominant=static_cast<int>(std::max_element(histogram.begin(),histogram.end())-histogram.begin());
    std::uint32_t face=0xff000000 | ((dominant/256*16+8)<<16) | (((dominant/16)%16*16+8)<<8) | (dominant%16*16+8);
    if (!histogram[dominant]) { throw std::runtime_error("？！无清晰背景！？"); }
    float radius=source.h*0.20f;
    int grid=std::max(1,source.h/50);
    for (int y=0;y<r.h;++y) { for (int x=0;x<r.w;++x) {
        int index=y*r.w+x,origin=r.origins[index];
        if (keep[origin]) { continue; }
        float px=std::min(r.w-0.5f,(x/grid)*grid+grid*0.5f),py=std::min(r.h-0.5f,(y/grid)*grid+grid*0.5f);
        float qx=std::abs(px-r.w*0.5f)-(r.w*0.5f-radius);
        float qy=std::abs(py-r.h*0.5f)-(r.h*0.5f-radius);
        float distance=radius-std::hypot(std::max(qx,0.0f),std::max(qy,0.0f))-std::min(std::max(qx,qy),0.0f);
        if (distance<0) { r.pixels[index]=0; continue; }
        if (distance<rim) {
            int sample=std::max(0,std::min(rim-1,static_cast<int>(distance)));
            auto profile=source.at(source.w/2,y<r.h/2 ? sample : source.h-1-sample);
            r.pixels[index]=profile | 0xff000000;
        } else {
            int grain=((x/grid)*13+(y/grid)*17)%11==0 ? 2 : 0;
            int light=static_cast<int>(6.0f*(1.0f-y/static_cast<float>(r.h)))+grain;
            auto value=[&](int shift) { return std::max(0,std::min(255,channel(face,shift)+light)); };
            r.pixels[index]=0xff000000 | (value(16)<<16) | (value(8)<<8) | value(0);
        }
    } }
}
void save(const Raster& r,const std::wstring& file) {
    Gdiplus::Bitmap bitmap(r.w,r.h,r.w*4,PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(r.pixels.data())));
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    if (bitmap.Save(file.c_str(),&png,nullptr)!=Gdiplus::Ok) { throw std::runtime_error("？！保存失败！？"); }
}
void fit(const std::wstring& input,const std::wstring& output,ncnl::ButtonArt id) {
    Raster source=read(input),r=source;
    auto design=ncnl::button_art_design_size(id);
    if (static_cast<long long>(source.w)*design.Height==static_cast<long long>(source.h)*design.Width) {
        save(source,output); std::wcout<<L"PASS "<<ncnl::button_art_filename(id)<<L": 已经导出了，没有图像更改\n"; return;
    }
    auto regions=foreground(r,id),original_regions=regions; int divisor=gcd(design.Width,design.Height);
    int a=design.Width/divisor,b=design.Height/divisor;
    int multiple=std::max((r.w+a-1)/a,(r.h+b-1)/b),width=a*multiple,height=b*multiple;
    int dx=width-r.w,dy=height-r.h;
    auto keep=preserved_pixels(source,original_regions);
    insert(r,regions,dx/2,true,true); insert(r,regions,dx-dx/2,false,true);
    transpose(r,regions);
    insert(r,regions,dy/2,true,false); insert(r,regions,dy-dy/2,false,false);
    transpose(r,regions);
    repair_frame(r,source,keep);
    if (r.w!=width || r.h!=height || static_cast<long long>(r.w)*design.Height!=static_cast<long long>(r.h)*design.Width) {
        throw std::runtime_error("？！无效几何！？");
    }
    for (std::size_t i=0;i<regions.size();++i) {
        auto old=original_regions[i].box,now=regions[i].box;
        for (int y=0;y<=old.y1-old.y0;++y) { for (int x=0;x<=old.x1-old.x0;++x) {
            if (keep[(old.y0+y)*source.w+old.x0+x] && source.at(old.x0+x,old.y0+y)!=r.at(now.x0+x,now.y0+y)) {
                throw std::runtime_error("？！有东西被改掉了！？");
            }
        } }
    }
    int x0=r.w,y0=r.h,x1=-1,y1=-1;
    for (int y=0;y<r.h;++y) { for (int x=0;x<r.w;++x) { if (channel(r.at(x,y),24)!=0) {
        x0=std::min(x0,x); x1=std::max(x1,x); y0=std::min(y0,y); y1=std::max(y1,y);
    } } }
    if (x0!=0 || y0!=0 || x1!=r.w-1 || y1!=r.h-1) { throw std::runtime_error("？！实际不透明边界与目标不匹配！？"); }
    save(r,output);
    std::wcout<<L"PASS "<<ncnl::button_art_filename(id)<<L": "<<source.w<<L"x"<<source.h<<L" -> "
        <<r.w<<L"x"<<r.h<<L", 字母和两种装饰像素一模一样\n";
}
}
int main(int argc,char* argv[]) {
    if (argc!=3 && argc!=4) { std::cerr<<"用法：button_asset_fit 源目录 输出目录 [按钮名]\n"; return 1; }
    std::string from=argv[1],to=argv[2]; std::wstring input(from.begin(),from.end()),output(to.begin(),to.end());
    ULONG_PTR token=0; Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) { return 1; }
    int result=0;
    try {
        if (input==output) { throw std::runtime_error("使用单独的输出目录；保留源艺术作品"); }
        bool selected=false;
        for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) {
            auto id=static_cast<ncnl::ButtonArt>(i); auto filename=std::wstring(ncnl::button_art_filename(id))+L".png";
            if (argc==4 && filename!=std::wstring(argv[3],argv[3]+std::strlen(argv[3]))+L".png") { continue; }
            selected=true;
            fit(input+L"/"+filename,output+L"/"+filename,id);
        }
        if (!selected) { throw std::runtime_error("没有找到对应的按钮名称。"); }
    } catch (const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<"\n"; result=2; }
    Gdiplus::GdiplusShutdown(token); return result;
}
