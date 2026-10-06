#pragma once
#include <windows.h>
#include <cstdint>

namespace ncnlplug {
struct Telemetry { double bpm,beat; bool playing; };
struct Callbacks {
    void* context;
    void (*state)(void*,const unsigned char*,std::uint32_t);
    void (*midi)(void*,std::uint32_t);
    void (*preview)(void*,bool);
    Telemetry (*telemetry)(void*);
};
struct BridgeApi {
    std::uint32_t version;
    HWND (*create)(HWND,const wchar_t*,const Callbacks*,const unsigned char*,std::uint32_t);
    void (*resize)(int,int);
    void (*detach)();
    void (*destroy)();
    bool (*restore)(const unsigned char*,std::uint32_t);
    void (*flush)();
};
using GetBridgeApi=const BridgeApi* (*)();
}
