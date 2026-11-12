#pragma once
#include "lilith_shm.h"
/* Reject abrupt support displacement before either seating or physics can consume it. */
typedef struct {ll_window window;uint32_t seen,until,moved;int blocked;} ll_motion_item;
typedef struct {ll_motion_item items[LL_WINDOW_CAP];unsigned count,events;uint32_t speed,settle_ms;} ll_motion_guard;
static int ll_motion_delta(int32_t a,int32_t b) {int64_t d=(int64_t)a-b;if(d<0)d=-d;return d>1000000?1000000:(int)d;}
static unsigned ll_motion_update(ll_motion_guard *g,const ll_window *raw,unsigned count,uint32_t now,ll_window *out) {
    ll_motion_item next[LL_WINDOW_CAP];unsigned emitted=0;if(count>LL_WINDOW_CAP)count=LL_WINDOW_CAP;
    uint32_t speed=g->speed?g->speed:900,settle=g->settle_ms?g->settle_ms:250;
    for(unsigned i=0;i<count;i++) {
        ll_motion_item item={0};item.window=raw[i];item.seen=now;
        for(unsigned j=0;j<g->count;j++)if(g->items[j].window.id==raw[i].id) {
            ll_motion_item old=g->items[j];item.until=old.until;item.blocked=old.blocked;item.moved=old.moved;
            uint32_t dt=now-old.seen;if(dt<1)dt=1;
            int step=ll_motion_delta(raw[i].x,old.window.x),d=ll_motion_delta(raw[i].y,old.window.y);if(d>step)step=d;
            d=ll_motion_delta(raw[i].w,old.window.w);if(d>step)step=d;
            d=ll_motion_delta(raw[i].h,old.window.h);if(d>step)step=d;
            if(step>0)item.moved=now;
            if(step>=96 || (step>16 && (uint64_t)step*1000>(uint64_t)speed*dt)) {
                if(!item.blocked)++g->events;
                item.blocked=1;item.until=now+settle;
            }
            break;
        }
        if(item.blocked && (int32_t)(now-item.until)>=0)item.blocked=0;
        next[i]=item;if(!item.blocked)out[emitted++]=raw[i];
    }
    for(unsigned i=0;i<count;i++)g->items[i]=next[i];
    g->count=count;
    return emitted;
}
