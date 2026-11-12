#pragma once
#include <stdint.h>
#define LL_NATIVE_MAGIC 0x4c4c4e41u
#define LL_NATIVE_PLAYERS 16
#define LL_NATIVE_TEXT 256
#define LL_NATIVE_FILE_COUNT 2000
#define LL_NATIVE_FILE_UNITS 65536
// Clipboard/file payload is transferred only on a targeted user paste or drop, never logged.
typedef struct {
    volatile uint32_t state; // 0 free, 1 request/writing, 2 ready, 3 host consuming, 4 result
    uint32_t count,units,cut,token,copy;
    int32_t x,y;
    uint32_t offsets[LL_NATIVE_FILE_COUNT];
    uint16_t paths[LL_NATIVE_FILE_UNITS];
} ll_native_files;
typedef struct {uint16_t id[LL_NATIVE_TEXT],label[LL_NATIVE_TEXT];} ll_native_player;
typedef struct {
    uint32_t ready,autostart,context_ready,fullscreen,idle_valid,idle_ms,locked,player_count;
    uint16_t process[LL_NATIVE_TEXT],app[LL_NATIVE_TEXT];
    ll_native_player players[LL_NATIVE_PLAYERS];
} ll_native_snapshot;
typedef struct {volatile uint32_t state;uint32_t kind,value;uint16_t target[LL_NATIVE_TEXT];} ll_native_command;
typedef struct {
    uint32_t magic;volatile uint32_t seq;
    ll_native_snapshot snapshot;
    volatile uint32_t paste_enabled,paste_over,paste_fired,clipboard_has_files,clipboard_clear,clipboard_token,resume_serial;
    volatile uint32_t drop_probe_seq,drop_probe_reply,drop_probe_hit;int32_t drop_probe_x,drop_probe_y;
    ll_native_command commands[8]; // kind 1 autostart; 2 open/raise player; 3 PlayPause; 4 Next; 5 Previous; 6 idempotent Pause (stop listening)
    ll_native_files clipboard,drops[2];
} ll_native;
