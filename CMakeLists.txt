cmake_minimum_required(VERSION 3.25)
project(NoChordNoLifePlugin VERSION 0.3.1 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "插件版目前需要 Windows 64 位编译工具链。")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO ON CACHE BOOL "" FORCE)
set(SMTG_PACKAGE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vst3sdk/CMakeLists.txt")
    message(FATAL_ERROR "缺少 Steinberg VST3 SDK，请先运行 tools/bootstrap.ps1。")
endif()
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()
include(tools/patch_sdk.cmake)
add_subdirectory(vendor/vst3sdk)
smtg_enable_vst3_sdk()
if(MINGW)
    target_link_options(validator PRIVATE -municode)
    target_link_options(moduleinfotool PRIVATE -municode)
endif()

file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "assets/*.png" "assets/*.gif" "assets/*.ani" "assets/*.ttf"
    "assets/defaults/presents/*.mid" "assets/defaults/presents/order.txt")
list(SORT asset_files)
set(resource_list "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico\"\n")
set(asset_index "#pragma once\nnamespace ncnl {\nstruct AssetEntry { const wchar_t* path; unsigned id; };\nstatic const AssetEntry ASSET_INDEX[]={\n")
set(resource_id 1000)
foreach(asset IN LISTS asset_files)
    string(APPEND resource_list "${resource_id} RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/assets/${asset}\"\n")
    string(APPEND asset_index "    {L\"${asset}\",${resource_id}},\n")
    math(EXPR resource_id "${resource_id}+1")
endforeach()
string(APPEND asset_index "};\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets.rc" CONTENT "${resource_list}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/asset_index.hpp" CONTENT "${asset_index}")
add_library(NCNL_UI SHARED
    src/host/native_bridge.cpp
    src/algorithms/chord_progression.cpp src/algorithms/chord_emotion.cpp
    src/generation/chord_generator.cpp src/config/progression_config.cpp
    src/midi/midi_rhythm.cpp src/ui/mouse_feedback.cpp src/ui/preset_library.cpp
    src/ui/midi_file_drag.cpp src/plugins/plugin_manager.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/assets.rc")
set_target_properties(NCNL_UI PROPERTIES PREFIX "" OUTPUT_NAME NCNL_UI)
target_compile_definitions(NCNL_UI PRIVATE UNICODE _UNICODE NOMINMAX)
target_include_directories(NCNL_UI PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(NCNL_UI PRIVATE gdiplus comdlg32 winmm shell32 ole32 uuid)
if(MINGW)
    target_compile_options(NCNL_UI PRIVATE -O2 -ffunction-sections -fdata-sections -finput-charset=UTF-8)
    target_link_options(NCNL_UI PRIVATE -Wl,--gc-sections)
endif()
smtg_add_vst3plugin(NoChordNoLife src/vst3/plugin.cpp
    vendor/vst3sdk/public.sdk/source/vst/vstsinglecomponenteffect.cpp)
get_target_property(plugin_package NoChordNoLife SMTG_PLUGIN_PACKAGE_PATH)
set_target_properties(NoChordNoLife PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${plugin_package}/Contents/x86_64-win"
    RUNTIME_OUTPUT_DIRECTORY "${plugin_package}/Contents/x86_64-win")
target_link_libraries(NoChordNoLife PRIVATE sdk shell32 ole32 user32)
target_compile_definitions(NoChordNoLife PRIVATE NOMINMAX UNICODE _UNICODE)
add_dependencies(NoChordNoLife NCNL_UI)
if(MINGW)
    set(runtime_license_command COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/w64devkit/COPYING.MinGW-w64-runtime.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/MinGW-RUNTIME-LICENSE.txt")
endif()
add_custom_target(package_plugin ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:NCNL_UI> $<TARGET_FILE_DIR:NoChordNoLife>/NCNL_UI.dll)
get_target_property(plugin_package NoChordNoLife SMTG_PLUGIN_PACKAGE_PATH)
add_custom_command(TARGET package_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${plugin_package}/Contents" "${CMAKE_CURRENT_SOURCE_DIR}/dist/NoChordNoLife.vst3/Contents"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${plugin_package}/PlugIn.ico" "${CMAKE_CURRENT_SOURCE_DIR}/dist/NoChordNoLife.vst3/PlugIn.ico"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${plugin_package}/desktop.ini" "${CMAKE_CURRENT_SOURCE_DIR}/dist/NoChordNoLife.vst3/desktop.ini"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/dist/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vst3sdk/LICENSE.txt" "${CMAKE_CURRENT_SOURCE_DIR}/dist/VST3-SDK-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/dist/README.md"
    ${runtime_license_command})
add_dependencies(package_plugin NoChordNoLife NCNL_UI)
