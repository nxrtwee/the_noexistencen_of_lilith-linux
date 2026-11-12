#pragma once
#include <stdint.h>
#define LL_TRAY_MAGIC 0x4c4c5452u
#define LL_TRAY_ITEMS 128
#define LL_TRAY_LABEL 256
#define LL_TRAY_PATH 4096
#define LL_TRAY_EVENTS 32
#define LL_TRAY_NOTIFICATIONS 8
// All strings are bounded UTF-16. Snapshot has a single producer under a proxy lock.
// IDs are 1-based, scoped to generation; parent=0 means root. kind: action=0, submenu=1, separator=2.
typedef struct {uint32_t id,parent,kind,enabled;uint16_t label[LL_TRAY_LABEL];} ll_tray_item;
typedef struct {
    uint32_t generation,active,count,badge_visible;
    uint16_t title[LL_TRAY_LABEL],icon[LL_TRAY_PATH],badge_icon[LL_TRAY_PATH],badge_tip[LL_TRAY_LABEL];
    ll_tray_item items[LL_TRAY_ITEMS];
} ll_tray_snapshot;
typedef struct {uint32_t generation,id;} ll_tray_event;
typedef struct {
    volatile uint32_t state; // 0 free, 1 writing, 2 ready, 3 consuming
    uint32_t generation,duration;
    uint16_t title[LL_TRAY_LABEL],text[1024];
} ll_tray_notification;
typedef struct {
    uint32_t magic;
    volatile uint32_t seq; // odd means writing; host copies only equal even seq snapshots
    ll_tray_snapshot snapshot;
    volatile uint32_t event_write,event_read,host_status,event_dropped; // event queue is host -> game SPSC
    ll_tray_event events[LL_TRAY_EVENTS];
    ll_tray_notification notifications[LL_TRAY_NOTIFICATIONS];
} ll_tray;
#define LL_TRAY_BADGE_ID 0xffffffffu
