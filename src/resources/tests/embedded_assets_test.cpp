#include "../../ui/chord_gui.cpp"
#include <cassert>
#include <iostream>

int main() {
    for (int size:{16,24,32,48,64,128,256}) {
        HICON icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,size,size,0));
        assert(icon);
        ICONINFO info={}; assert(GetIconInfo(icon,&info));
        BITMAP bitmap={}; assert(GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap));
        assert(bitmap.bmWidth==size && bitmap.bmHeight==size);
        DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); DestroyIcon(icon);
    }
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&gdiplus_token,&startup,nullptr)==Gdiplus::Ok);
    for (const auto& entry:ncnl::ASSET_INDEX) {
        auto bytes=ncnl::embedded_asset(std::wstring(L":/assets/")+entry.path);
        assert(bytes.data && bytes.size);
    }
    assert(load_interface_font(L":/assets/res/font.ttf"));
    load_application_skins(L":/assets"); load_interface_images();
    assert(background_image && pentagon_image && ncnl_button_art && plugin_tab_art && arrow_image);
    for (const auto& skin:configuration_skins) { assert(skin); }
    for (auto& image:emotion_images) { assert(image && image->GetFrameCount(&Gdiplus::FrameDimensionTime)>1); }
    for (const auto& name:{L"Link.ani",L"Text Select.ani",L"Unavailable.ani",L"Vertical Resize.ani"}) {
        HCURSOR cursor=ncnl::asset_cursor(std::wstring(L":/assets/cursor/")+name); assert(cursor);
        ICONINFO info={}; assert(GetIconInfo(cursor,&info));
        BITMAP bitmap={}; assert(GetObjectW(info.hbmColor?info.hbmColor:info.hbmMask,sizeof(bitmap),&bitmap));
        int height=info.hbmColor?bitmap.bmHeight:bitmap.bmHeight/2;
        std::cout<<"光标尺寸："<<bitmap.bmWidth<<"x"<<height<<"\n";
        assert(bitmap.bmWidth==ncnl::application_cursor_size(SM_CXCURSOR) && height==ncnl::application_cursor_size(SM_CYCURSOR));
        assert(info.xHotspot<static_cast<DWORD>(bitmap.bmWidth) && info.yHotspot<static_cast<DWORD>(height));
        if (info.hbmMask) { DeleteObject(info.hbmMask); }
        if (info.hbmColor) { DeleteObject(info.hbmColor); }
        DestroyCursor(cursor);
    }
    auto storage=executable_directory()+L"\\storage_验证";
    assert(CreateDirectoryW(storage.c_str(),nullptr)); initialize_runtime_storage(storage);
    assert(GetFileAttributesW((storage+L"\\presents").c_str())&FILE_ATTRIBUTE_DIRECTORY);
    assert(GetFileAttributesW((storage+L"\\plugins").c_str())&FILE_ATTRIBUTE_DIRECTORY);
    ncnl::adjust_progression_weight(0,0.47); assert(ncnl::save_progression_config(progression_config_path));
    initialize_runtime_storage(storage); assert(std::abs(ncnl::progression_weights().values[0]-0.47)<0.00001);
    assert(DeleteFileW((storage+L"\\config.json").c_str()));
    assert(RemoveDirectoryW((storage+L"\\presents").c_str())); assert(RemoveDirectoryW((storage+L"\\plugins").c_str()));
    assert(RemoveDirectoryW(storage.c_str()));
    std::cout<<"通过：全部内嵌文件、背景与按钮、字体、GIF 动画、ANI 光标、中文路径与运行数据创建及保留。\n";
}
