#pragma once
#include <stdint.h>
#define LL_DESKTOP_CAP 4
#define LL_DESKTOP_PATH 4096
#define LL_DESKTOP_TITLE 256
#define LL_DESKTOP_MAGIC 0x4c4c4453u
// state: 0 free, 1 producer reservation, 2 request, 3 host processing, 4 reply, 5 cancelled.
// Only producer reserves; host owns 2->3 and completion; cancellation is acknowledged by host.
// kind: 1 choose folder, 2 open local path, 3 open URI, 4 reveal local item.
typedef struct {
    volatile uint32_t state;
    uint32_t id,kind,detached,status;
    uint16_t title[LL_DESKTOP_TITLE],value[LL_DESKTOP_PATH],result[LL_DESKTOP_PATH];
} ll_desktop_request;
typedef struct {
    uint32_t magic;
    volatile uint32_t next_id,host_ready;
    ll_desktop_request requests[LL_DESKTOP_CAP];
} ll_desktop;
