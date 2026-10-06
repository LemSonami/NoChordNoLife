#pragma once
#include <stdint.h>

#ifdef _WIN32
#define NCNL_CALL __cdecl
#ifdef NCNL_BUILD_PLUGIN
#define NCNL_EXPORT __declspec(dllexport)
#else
#define NCNL_EXPORT
#endif
#else
#define NCNL_CALL
#define NCNL_EXPORT
#endif

#define NCNL_PLUGIN_ABI 1u

typedef struct NcnlContextV1 {
    uint32_t size;
    uint32_t reserved;
    double bpm;
    uint32_t rhythm_events;
    uint32_t midi_notes;
    char mode[64];
} NcnlContextV1;

typedef struct NcnlHostV1 {
    uint32_t size;
    uint32_t abi;
    void* user;
    int32_t (NCNL_CALL *get_context)(void*,NcnlContextV1*);
    uint32_t (NCNL_CALL *read_midi)(void*,uint8_t*,uint32_t);
} NcnlHostV1;

typedef struct NcnlPluginV1 {
    uint32_t size;
    uint32_t abi;
    const char* id;
    void* (NCNL_CALL *create_page)(void*,const NcnlHostV1*);
    void (NCNL_CALL *destroy_page)(void*);
    void (NCNL_CALL *resize_page)(void*,int32_t,int32_t);
    void (NCNL_CALL *activate_page)(void*,int32_t);
} NcnlPluginV1;

typedef int32_t (NCNL_CALL *NcnlGetPluginV1)(uint32_t,NcnlPluginV1*);

#ifdef __cplusplus
extern "C" {
#endif
NCNL_EXPORT int32_t NCNL_CALL ncnl_get_plugin(uint32_t host_abi,NcnlPluginV1* output);
#ifdef __cplusplus
}
#endif
