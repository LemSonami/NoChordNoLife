#define UNICODE
#define _UNICODE
#include "../ui/button_artwork.hpp"
#include <cassert>
#include <iostream>
#include <sstream>

namespace ncnl {
struct ButtonArtworkTestAccess {
    static unsigned builds(const ButtonArtwork& art) { return art.cache_builds; }
    static Gdiplus::Bitmap* cache(const ButtonArtwork& art) { return art.scaled.get(); }
    static Gdiplus::Rect bounds(const ButtonArtwork& art) { return art.crop; }
};
}

int main(int argc,char* argv[]) {
    ULONG_PTR token=0; Gdiplus::GdiplusStartupInput input;
    assert(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok);
    if (argc>1 && std::string(argv[1])=="--verify-aspect") {
        std::string folder=argc>2 ? argv[2] : "assets/skins/buttons";
        std::wstring directory(folder.begin(),folder.end()); bool all_match=true;
        for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) {
            auto id=static_cast<ncnl::ButtonArt>(i); auto size=ncnl::button_art_design_size(id);
            std::wstring file=directory+L"/"+ncnl::button_art_filename(id)+L".png";
            Gdiplus::Bitmap bitmap(file.c_str());
            if (bitmap.GetLastStatus()!=Gdiplus::Ok) { all_match=false; continue; }
            int w=bitmap.GetWidth(),h=bitmap.GetHeight(),left=w,top=h,right=-1,bottom=-1;
            Gdiplus::Rect rectangle(0,0,w,h); Gdiplus::BitmapData data={};
            assert(bitmap.LockBits(&rectangle,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)==Gdiplus::Ok);
            for (int y=0;y<h;++y) {
                auto row=static_cast<const BYTE*>(data.Scan0)+y*data.Stride;
                for (int x=0;x<w;++x) { if (row[x*4+3]!=0) {
                    left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
                } }
            }
            bitmap.UnlockBits(&data);
            int width=right-left+1,height=bottom-top+1;
            bool match=width>0 && height>0 && static_cast<long long>(width)*size.Height==static_cast<long long>(height)*size.Width;
            std::wcout<<(match ? L"PASS " : L"FAIL ")<<ncnl::button_art_filename(id)<<L": nonzero-alpha "
                <<width<<L"x"<<height<<L", expected "<<size.Width<<L":"<<size.Height<<L"\n";
            all_match=all_match && match;
        }
        Gdiplus::GdiplusShutdown(token); return all_match ? 0 : 2;
    }
    if (argc>1 && std::string(argv[1])=="--guides") {
        // Code-native layout mockups, used only as geometry references for imagegen.
        // These are not production artwork and never transform an authored PNG.
        const wchar_t* labels[]={L"生成",L"配置",L"预设",L"调式选择",L"和弦行进评价雷达图",
            L"决定了，就是你！",L"刷新",L"保存当前节奏型",L"Ionian",L"Dorian",L"Phrygian",
            L"Lydian",L"Mixolydian",L"Aeolian",L"Locrian"};
        const CLSID codec={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        CreateDirectoryW(L"tests\\button_geometry_guides",nullptr);
        for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) {
            auto id=static_cast<ncnl::ButtonArt>(i); auto size=ncnl::button_art_design_size(id);
            int factor=size.Width<200 ? 8 : 5,w=size.Width*factor,h=size.Height*factor;
            int cw=w+64,ch=std::max(h+64,static_cast<int>(std::ceil(cw/2.5))); int y=(ch-h)/2;
            Gdiplus::Bitmap bitmap(cw,ch,PixelFormat32bppARGB);
            {
                Gdiplus::Graphics graphics(&bitmap); graphics.Clear(Gdiplus::Color(0,0,0,0));
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                float radius=h*0.2f,d=radius*2; Gdiplus::GraphicsPath path;
                path.AddArc(32.0f,static_cast<float>(y),d,d,180,90);
                path.AddArc(32.0f+w-d,static_cast<float>(y),d,d,270,90);
                path.AddArc(32.0f+w-d,y+h-d,d,d,0,90);
                path.AddArc(32.0f,y+h-d,d,d,90,90); path.CloseFigure();
                Gdiplus::SolidBrush fill(Gdiplus::Color(255,218,220,179)); graphics.FillPath(&fill,&path);
                Gdiplus::FontFamily family(L"Microsoft YaHei UI");
                Gdiplus::Font font(&family,static_cast<float>(h*0.48),Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
                Gdiplus::SolidBrush ink(Gdiplus::Color(255,25,50,31)); Gdiplus::StringFormat format;
                format.SetAlignment(Gdiplus::StringAlignmentCenter); format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                graphics.DrawString(labels[i],-1,&font,Gdiplus::RectF(32.0f,static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)),&format,&ink);
            }
            std::wstring path=L"tests\\button_geometry_guides\\"+std::wstring(ncnl::button_art_filename(id))+L"_preview.png";
            assert(bitmap.Save(path.c_str(),&codec,nullptr)==Gdiplus::Ok);
        }
        Gdiplus::GdiplusShutdown(token); return 0;
    }
    if (argc>1) {
        std::string folder=argv[1]; std::wstring directory(folder.begin(),folder.end());
        {
            ncnl::ButtonArtworks images; images.load(directory);
            for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) {
                auto id=static_cast<ncnl::ButtonArt>(i); assert(images[id]);
                auto crop=ncnl::ButtonArtworkTestAccess::bounds(images[id]);
                auto design=ncnl::button_art_design_size(id);
                std::wcout<<ncnl::button_art_filename(id)<<L" "<<crop.Width<<L" "<<crop.Height<<L" "
                    <<std::abs(static_cast<double>(crop.Width)*design.Height/(crop.Height*design.Width)-1)*100<<L"\n";
            }
        }
        Gdiplus::GdiplusShutdown(token); return 0;
    }
    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    std::wostringstream name; name<<L"tests\\button_art_tmp_"<<GetCurrentProcessId()<<L".png";
    std::wstring path=name.str();
    {
        Gdiplus::Bitmap fixture(100,100,PixelFormat32bppARGB);
        { Gdiplus::Graphics graphics(&fixture); graphics.Clear(Gdiplus::Color(0,0,0,0));
          Gdiplus::SolidBrush green(Gdiplus::Color(255,80,190,110)); graphics.FillRectangle(&green,10,40,80,20); }
        fixture.SetPixel(1,1,Gdiplus::Color(1,80,190,110));
        fixture.SetPixel(98,98,Gdiplus::Color(8,80,190,110)); // Export speckles must not shrink the real plaque.
        assert(fixture.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok);
    }
    {
        ncnl::ButtonArtwork art; assert(art.load(path));
        auto moved=path+L".moved"; assert(MoveFileW(path.c_str(),moved.c_str())); // Source file not held open.
        assert(ncnl::ButtonArtworkTestAccess::bounds(art).Equals(Gdiplus::Rect(10,40,80,20)));
        Gdiplus::Bitmap canvas(320,200,PixelFormat32bppARGB); Gdiplus::Graphics graphics(&canvas);
        assert(art.draw(graphics,{10,10,160,40}));
        auto cached=ncnl::ButtonArtworkTestAccess::cache(art); assert(cached->GetWidth()==160 && cached->GetHeight()==40);
        for (int i=0;i<100;++i) { assert(art.draw(graphics,{10,10,160,40},i%2,i%3==0)); }
        assert(ncnl::ButtonArtworkTestAccess::builds(art)==1 && ncnl::ButtonArtworkTestAccess::cache(art)==cached);
        // A square hit region must not turn the 4:1 plaque into a square.
        assert(art.draw(graphics,{0,0,120,120}));
        auto fitted=ncnl::fit_button_artwork({0,0,120,120},80,20);
        assert(fitted.Width==120 && fitted.Height==30 && fitted.Y==45);
        auto cache=ncnl::ButtonArtworkTestAccess::cache(art);
        Gdiplus::Color center,margin;
        cache->GetPixel(60,60,&center); cache->GetPixel(60,10,&margin);
        assert(center.GetA()>250 && margin.GetA()==0);
        // Non-integer zoom still uses equal X/Y scale in logical coordinates.
        auto fractional=ncnl::fit_button_artwork({0,0,120.5f,52.25f},80,20);
        assert(std::abs(fractional.Width/80-fractional.Height/20)<0.00001f);
        graphics.ScaleTransform(1.5f,1.5f); assert(art.draw(graphics,{10,10,160,40}));
        assert(ncnl::ButtonArtworkTestAccess::cache(art)->GetWidth()==240 && ncnl::ButtonArtworkTestAccess::builds(art)==3);
        // Same rounded cache dimensions can hide a changed logical aspect.
        graphics.ResetTransform();
        assert(art.draw(graphics,{0,0,120.1f,52.1f}));
        assert(art.draw(graphics,{0,0,120.8f,52.7f}));
        assert(ncnl::ButtonArtworkTestAccess::builds(art)==5);
        for (int i=0;i<20;++i) { assert(art.draw(graphics,{0,0,120.8f,52.7f})); }
        assert(ncnl::ButtonArtworkTestAccess::builds(art)==5);
        art.reset(); assert(!art && !art.draw(graphics,{0,0,100,40}));
        assert(!art.load(L"tests\\missing_button.png")); assert(DeleteFileW(moved.c_str()));
    }
    {
        ncnl::ButtonArtworks assets; assets.load(L"assets\\skins\\buttons");
        Gdiplus::Bitmap canvas(360,120,PixelFormat32bppARGB); Gdiplus::Graphics graphics(&canvas);
        for (int i=0;i<static_cast<int>(ncnl::ButtonArt::Count);++i) {
            auto id=static_cast<ncnl::ButtonArt>(i); auto& art=assets[id]; assert(art);
            auto crop=ncnl::ButtonArtworkTestAccess::bounds(art);
            auto design=ncnl::button_art_design_size(id);
            double error=std::abs(static_cast<double>(crop.Width)*design.Height/(crop.Height*design.Width)-1);
            std::wcout<<ncnl::button_art_filename(id)<<L": "<<crop.Width<<L"x"<<crop.Height<<L", visible aspect error "<<error*100<<L"%\n";
            assert(static_cast<long long>(crop.Width)*design.Height==static_cast<long long>(crop.Height)*design.Width);
            Gdiplus::RectF bounds(0,0,static_cast<float>(design.Width),static_cast<float>(design.Height));
            // A generator's aspect request is not a geometric guarantee. The
            // displayed plaque must ALWAYS retain the actual alpha-bound aspect,
            // even for a future asset whose proportion differs from its UI slot.
            auto fitted=ncnl::fit_button_artwork(bounds,crop.Width,crop.Height);
            assert(std::abs(fitted.Width/crop.Width-fitted.Height/crop.Height)<0.00001f);
            assert(fitted.Width<=bounds.Width+0.0001f && fitted.Height<=bounds.Height+0.0001f);
            assert(fitted.X>=-0.0001f && fitted.Y>=-0.0001f);
            assert(assets.draw(graphics,id,bounds));
            Gdiplus::Color corner,center; auto image=ncnl::ButtonArtworkTestAccess::cache(art);
            image->GetPixel(0,0,&corner); image->GetPixel(design.Width/2,design.Height/2,&center);
            assert(corner.GetA()<30 && center.GetA()>240); // Genuine transparent corners, opaque readable center.
            auto builds=ncnl::ButtonArtworkTestAccess::builds(art);
            assert(assets.draw(graphics,id,bounds,true,true));
            assert(builds==ncnl::ButtonArtworkTestAccess::builds(art));
        }
        assets.reset(); assert(!assets[ncnl::ButtonArt::Generate]);
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"PASS: all 15 asset alpha-bound aspects exactly match real button sizes; uniform fit/mismatched aspect/fractional zoom, true transparent corners, unlocked sources and state/resize cache.\n";
}
