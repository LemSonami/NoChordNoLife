#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace ncnl {



class PixelSkin {
    friend struct PixelSkinTestAccess;
    std::unique_ptr<Gdiplus::Bitmap> pixels,scaled;
    int cached_width=0,cached_height=0;
    float cached_opacity=-1;
    unsigned cache_builds=0;
public:
    explicit operator bool() const { return static_cast<bool>(pixels); }
    void reset() {
        scaled.reset(); pixels.reset(); cached_width=0; cached_height=0;
        cached_opacity=-1; cache_builds=0;
    }
    bool load(const std::wstring& path) {
        if (path.empty()) { return false; }
        Gdiplus::Image source(path.c_str());
        if (source.GetLastStatus()!=Gdiplus::Ok || !source.GetWidth() || !source.GetHeight() ||
            source.GetWidth()>8192 || source.GetHeight()>8192) { return false; }
        std::unique_ptr<Gdiplus::Bitmap> decoded(new Gdiplus::Bitmap(
            source.GetWidth(),source.GetHeight(),PixelFormat32bppPARGB));
        if (decoded->GetLastStatus()!=Gdiplus::Ok) { return false; }
        {
            Gdiplus::Graphics graphics(decoded.get());
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            if (graphics.DrawImage(&source,0,0,source.GetWidth(),source.GetHeight())!=Gdiplus::Ok) { return false; }
        }
        reset(); pixels=std::move(decoded); return true;
    }
    bool draw(Gdiplus::Graphics& graphics,int width,int height,float opacity) {
        if (!pixels || width<=0 || height<=0 || width>8192 || height>8192) { return false; }
        opacity=std::max(0.0f,std::min(1.0f,opacity));
        if (!scaled || cached_width!=width || cached_height!=height || cached_opacity!=opacity) {
            std::unique_ptr<Gdiplus::Bitmap> next(new Gdiplus::Bitmap(width,height,PixelFormat32bppPARGB));
            if (next->GetLastStatus()!=Gdiplus::Ok) { return false; }
            float source_width=static_cast<float>(pixels->GetWidth());
            float source_height=static_cast<float>(pixels->GetHeight());
            float aspect=static_cast<float>(width)/height;
            if (source_width/source_height>aspect) { source_width=source_height*aspect; }
            else { source_height=source_width/aspect; }
            Gdiplus::ColorMatrix matrix={
                1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,opacity,0, 0,0,0,0,1
            };
            Gdiplus::ImageAttributes attributes;
            attributes.SetColorMatrix(&matrix);
            attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
            {
                Gdiplus::Graphics cache(next.get());
                cache.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                cache.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                cache.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
                if (cache.DrawImage(pixels.get(),Gdiplus::Rect(0,0,width,height),
                    (pixels->GetWidth()-source_width)/2,(pixels->GetHeight()-source_height)/2,
                    source_width,source_height,Gdiplus::UnitPixel,&attributes)!=Gdiplus::Ok) { return false; }
            }
            scaled=std::move(next); cached_width=width; cached_height=height;
            cached_opacity=opacity; ++cache_builds;
        }
        auto state=graphics.Save();
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        auto result=graphics.DrawImage(scaled.get(),0,0,width,height);
        graphics.Restore(state); return result==Gdiplus::Ok;
    }
};

}