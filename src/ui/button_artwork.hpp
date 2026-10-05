#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include "../resources/embedded_assets.hpp"

namespace ncnl {

enum class ButtonArt {
    Generate,Settings,Presets,ModeTab,RadarTab,Confirm,Refresh,Save,
    Ionian,Dorian,Phrygian,Lydian,Mixolydian,Aeolian,Locrian,Count
};

inline const wchar_t* button_art_filename(ButtonArt id) {
    static const wchar_t* names[]={L"generate",L"settings",L"presets",L"mode_tab",L"radar_tab",
        L"confirm",L"refresh",L"save",L"mode_ionian",L"mode_dorian",L"mode_phrygian",
        L"mode_lydian",L"mode_mixolydian",L"mode_aeolian",L"mode_locrian"};
    return names[static_cast<std::size_t>(id)];
}

inline Gdiplus::Size button_art_design_size(ButtonArt id) {
    static const Gdiplus::Size sizes[]={
        {120,52},{120,52},{140,60},{360,55},{360,55},{340,40},{120,42},{276,50},
        {162,40},{162,40},{162,40},{162,40},{162,40},{162,40},{162,40}
    };
    return sizes[static_cast<std::size_t>(id)];
}

inline Gdiplus::RectF fit_button_artwork(Gdiplus::RectF bounds,int source_width,int source_height) {
    float scale=std::min(bounds.Width/source_width,bounds.Height/source_height);
    float width=source_width*scale,height=source_height*scale;
    return {bounds.X+(bounds.Width-width)*0.5f,bounds.Y+(bounds.Height-height)*0.5f,width,height};
}



class ButtonArtwork {
    std::unique_ptr<Gdiplus::Bitmap> pixels,scaled;
    Gdiplus::Rect crop;
    int cached_width=0,cached_height=0;
    float cached_logical_width=0,cached_logical_height=0;
public:
    explicit operator bool() const { return static_cast<bool>(pixels); }
    Gdiplus::Size visible_size() const { return pixels ? Gdiplus::Size(crop.Width,crop.Height) : Gdiplus::Size(0,0); }
    void reset() { scaled.reset(); pixels.reset(); cached_width=cached_height=0; cached_logical_width=cached_logical_height=0; }
    bool load(const std::wstring& path,bool strict_alpha=false) {
        reset();
        auto image=asset_image(path); if (!image) { return false; }
        auto& source=*image;
        if (source.GetLastStatus()!=Gdiplus::Ok || !source.GetWidth() || !source.GetHeight() ||
            source.GetWidth()>8192 || source.GetHeight()>8192) { return false; }
        int width=static_cast<int>(source.GetWidth()),height=static_cast<int>(source.GetHeight());
        std::unique_ptr<Gdiplus::Bitmap> decoded(new Gdiplus::Bitmap(width,height,PixelFormat32bppPARGB));
        {
            Gdiplus::Graphics graphics(decoded.get());
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            if (graphics.DrawImage(&source,0,0,width,height)!=Gdiplus::Ok) { return false; }
        }
        Gdiplus::Rect bounds(0,0,width,height); Gdiplus::BitmapData data={};
        if (decoded->LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok) { return false; }
        int left=width,top=height,right=-1,bottom=-1;
        for (int y=0;y<height;++y) {
            const auto row=static_cast<const BYTE*>(data.Scan0)+y*data.Stride;
            for (int x=0;x<width;++x) {


                if (row[x*4+3]>(strict_alpha?0:8)) { left=std::min(left,x); top=std::min(top,y); right=std::max(right,x); bottom=std::max(bottom,y); }
            }
        }
        decoded->UnlockBits(&data);
        if (right<left) { return false; }
        crop=Gdiplus::Rect(left,top,right-left+1,bottom-top+1); pixels=std::move(decoded); return true;
    }
    bool draw(Gdiplus::Graphics& graphics,Gdiplus::RectF bounds,bool selected=false,bool pressed=false) {
        if (!pixels || bounds.Width<=0 || bounds.Height<=0) { return false; }
        Gdiplus::Matrix transform; graphics.GetTransform(&transform); Gdiplus::REAL elements[6]={}; transform.GetElements(elements);
        int width=static_cast<int>(std::ceil(bounds.Width*std::hypot(elements[0],elements[1])));
        int height=static_cast<int>(std::ceil(bounds.Height*std::hypot(elements[2],elements[3])));
        if (width<=0 || height<=0 || width>8192 || height>8192) { return false; }
        auto content=fit_button_artwork(bounds,crop.Width,crop.Height);
        if (!scaled || cached_width!=width || cached_height!=height ||
            cached_logical_width!=bounds.Width || cached_logical_height!=bounds.Height) {
            std::unique_ptr<Gdiplus::Bitmap> next(new Gdiplus::Bitmap(width,height,PixelFormat32bppPARGB));
            {
                Gdiplus::Graphics cache(next.get()); cache.Clear(Gdiplus::Color(0,0,0,0));
                cache.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                cache.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                cache.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                Gdiplus::ImageAttributes attributes; attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);




                Gdiplus::RectF fitted((content.X-bounds.X)*width/bounds.Width,(content.Y-bounds.Y)*height/bounds.Height,
                    content.Width*width/bounds.Width,content.Height*height/bounds.Height);
                if (cache.DrawImage(pixels.get(),fitted,static_cast<float>(crop.X),static_cast<float>(crop.Y),
                    static_cast<float>(crop.Width),static_cast<float>(crop.Height),
                    Gdiplus::UnitPixel,&attributes)!=Gdiplus::Ok) { return false; }
            }
            scaled=std::move(next); cached_width=width; cached_height=height;
            cached_logical_width=bounds.Width; cached_logical_height=bounds.Height;
        }
        auto state=graphics.Save(); graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        Gdiplus::ColorMatrix matrix={
            1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,1,0, 0,0,0,0,1
        };
        if (pressed) { matrix.m[0][0]=matrix.m[1][1]=matrix.m[2][2]=0.8f; }
        Gdiplus::ImageAttributes attributes; attributes.SetColorMatrix(&matrix);
        auto result=graphics.DrawImage(scaled.get(),bounds,0,0,width,height,Gdiplus::UnitPixel,&attributes);
        if (selected) {

            Gdiplus::Pen edge(Gdiplus::Color(255,184,245,194),2.0f);
            float radius=std::min(10.0f,content.Height*0.23f),d=radius*2;
            Gdiplus::GraphicsPath outline;
            outline.AddArc(content.X+1,content.Y+1,d,d,180,90);
            outline.AddArc(content.GetRight()-d-1,content.Y+1,d,d,270,90);
            outline.AddArc(content.GetRight()-d-1,content.GetBottom()-d-1,d,d,0,90);
            outline.AddArc(content.X+1,content.GetBottom()-d-1,d,d,90,90); outline.CloseFigure();
            graphics.DrawPath(&edge,&outline);
        }
        graphics.Restore(state); return result==Gdiplus::Ok;
    }
};

class ButtonArtworks {
    std::array<ButtonArtwork,static_cast<std::size_t>(ButtonArt::Count)> images;
public:
    void reset() { for (auto& image:images) { image.reset(); } }
    bool load(ButtonArt id,const std::wstring& directory) {
        return images[static_cast<std::size_t>(id)].load(directory+L"\\"+button_art_filename(id)+L".png");
    }
    void load(const std::wstring& directory) {
        for (std::size_t i=0;i<images.size();++i) { load(static_cast<ButtonArt>(i),directory); }
    }
    ButtonArtwork& operator[](ButtonArt id) { return images[static_cast<std::size_t>(id)]; }
    bool draw(Gdiplus::Graphics& graphics,ButtonArt id,Gdiplus::RectF bounds,bool selected=false,bool pressed=false) {
        return images[static_cast<std::size_t>(id)].draw(graphics,bounds,selected,pressed);
    }
};

}
