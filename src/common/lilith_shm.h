// Shared frame channel between the Wine-side proxy and the native Linux host.
// File: /dev/shm/lilith-linux-frame (Z:\dev\shm\lilith-linux-frame from Wine)
#pragma once
#include <stdint.h>
#include "lilith_desktop.h"
#include "lilith_tray.h"
#include "lilith_native.h"
#define LL_MAGIC 0x4C4C4652u /* 'LLFR' */
#define LL_VERSION 9
#define LL_HOTKEY_COUNT 9
#define LL_WINDOW_CAP 32
#define LL_BUFFERS 3
#define LL_MAX_W 7680
#define LL_MAX_H 4320
#define LL_BUF_SIZE ((uint64_t)LL_MAX_W * LL_MAX_H * 4)
#define LL_BUF_OFF 4096
#define LL_DESKTOP_OFF (LL_BUF_OFF + LL_BUFFERS * LL_BUF_SIZE)
#define LL_TRAY_OFF (LL_DESKTOP_OFF + sizeof(ll_desktop))
#define LL_NATIVE_OFF (LL_TRAY_OFF + sizeof(ll_tray))
#define LL_FILE_SIZE (LL_NATIVE_OFF + sizeof(ll_native))
#define LL_SHM_UNIX "/dev/shm/lilith-linux-frame"
#define LL_SHM_WIN "Z:\\dev\\shm\\lilith-linux-frame"

#define LL_EVENT_CAP 128
#define LL_KEYBOARD 1u
#define LL_PASSTHROUGH 2u
typedef struct { uint32_t type, code, extra, reserved; } ll_event;
typedef struct { uint64_t id; int32_t x,y,w,h; uint32_t flags,reserved; } ll_window;
typedef struct {
    uint32_t magic, version;
    volatile uint32_t seq;      // incremented after each published frame
    volatile uint32_t cur;      // index (0/1) of the last complete buffer
    int32_t x, y, w, h;         // window rect in Win32 desktop coords (last published)
    uint32_t stride;            // bytes per row in buffer (= w*4), rows top-down, BGRA premultiplied
    uint32_t flags;             // bit0: hidden
    uint64_t hwnd;
    volatile uint32_t pid;      // game pid (win)
    volatile uint32_t host_alive;
    int32_t frame_x[LL_BUFFERS], frame_y[LL_BUFFERS], frame_w[LL_BUFFERS], frame_h[LL_BUFFERS]; // per-buffer geometry
    volatile uint64_t x11_window;   // winex11 whole_window of the layered hwnd (0 = unknown / not X11)
    // Host writes pointer/buttons/event_write; proxy writes event_read/game_flags.
    volatile uint32_t game_flags;
    volatile int32_t mouse_x, mouse_y; // Win32 desktop pixels
    volatile uint32_t mouse_buttons;
    volatile uint32_t mouse_down[3], mouse_up[3];
    volatile uint32_t pointer_inside;
    volatile uint32_t event_write, event_read, event_dropped;
    volatile uint32_t host_heartbeat;
    volatile uint32_t input_frames, input_events;
    ll_event events[LL_EVENT_CAP]; // type 1=key down, 2=key up, 3=UTF-16 char, 4=wheel
    volatile uint32_t screen_count;
    int32_t screen_rects[8][8]; // x,y,w,h,work_x,y,w,h
    volatile uint32_t buffer_state[LL_BUFFERS]; // 0=free,1=producer,2=presenter
    volatile uint32_t shutdown_request, input_reset;
    volatile uint32_t windows_seq, window_count;
    ll_window windows[LL_WINDOW_CAP]; // topmost first, native desktop pixels
    // Game -> host: seqlocked original bindings (mods Ctrl=1 Alt=2 Shift=4 Super=8).
    volatile uint32_t hotkey_seq, hotkey_ready, hotkey_capturing;
    uint32_t hotkey_mods[LL_HOTKEY_COUNT], hotkey_keys[LL_HOTKEY_COUNT];
    // Host -> game: monotonically increasing invocation counters; no focus/input-ring dependency.
    volatile uint32_t hotkey_fired[LL_HOTKEY_COUNT], hotkey_status;
    volatile uint32_t focused_vk[8]; // only focused overlay key states; never a global keylogger
} ll_header;
typedef char ll_header_must_fit[(sizeof(ll_header) <= LL_BUF_OFF) ? 1 : -1];
