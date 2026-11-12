// lilith-linux: version.dll proxy + IL2CPP probe (PoC stage 1)
#include <windows.h>
#include "../common/lilith_shm.h"
#include "../common/lilith_motion.h"
#include "../common/lilith_rebound.h"

#define EXPORTS(X) X(GetFileVersionInfoA) X(GetFileVersionInfoExA) X(GetFileVersionInfoExW) \
 X(GetFileVersionInfoSizeA) X(GetFileVersionInfoSizeExA) X(GetFileVersionInfoSizeExW) \
 X(GetFileVersionInfoSizeW) X(GetFileVersionInfoW) X(VerFindFileA) X(VerFindFileW) \
 X(VerInstallFileA) X(VerInstallFileW) X(VerLanguageNameA) X(VerLanguageNameW) \
 X(VerQueryValueA) X(VerQueryValueW)

#define DECL(n) void *p_##n;
EXPORTS(DECL)
#define STUB(n) __attribute__((naked)) void px_##n(void){ __asm__ volatile("jmp *p_" #n "(%rip)"); }
EXPORTS(STUB)

static HANDLE g_log = INVALID_HANDLE_VALUE;
static void logf(const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap); va_end(ap);
    if (n < 0) return; buf[n++]='\r'; buf[n++]='\n';
    DWORD w; if (g_log != INVALID_HANDLE_VALUE) WriteFile(g_log, buf, n, &w, NULL);
}


/* ================= frame redirect: UpdateLayeredWindow -> shared memory ================= */
static ll_header *g_shm; static unsigned char *g_buf[LL_BUFFERS];
static LONG g_frames, g_skipped; static int g_flip; static HWND g_layered;
static void hide_layered(HWND h);
static void (*ll_pump)(void);
static POINT g_lastPos; static SIZE g_lastSize;

static void shm_open_(void) {
    HANDLE f = CreateFileA(LL_SHM_WIN, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { logf("shm: CreateFile failed %lu", GetLastError()); return; }
    uint64_t sz = LL_FILE_SIZE;
    HANDLE m = CreateFileMappingA(f, NULL, PAGE_READWRITE, (DWORD)(sz>>32), (DWORD)sz, NULL);
    if (!m) { logf("shm: CreateFileMapping failed %lu", GetLastError()); return; }
    g_shm = (ll_header*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_shm) { logf("shm: MapViewOfFile failed %lu", GetLastError()); return; }
    for(int i=0;i<LL_BUFFERS;i++)g_buf[i]=(unsigned char*)g_shm+LL_BUF_OFF+i*LL_BUF_SIZE;
    // Reset the channel at each game launch (no stale input from prior session).
    __builtin_memset(g_shm, 0, LL_BUF_OFF);
    ll_desktop *desktop=(ll_desktop*)((unsigned char*)g_shm+LL_DESKTOP_OFF);
    __builtin_memset(desktop,0,sizeof(*desktop));desktop->magic=LL_DESKTOP_MAGIC;
    ll_tray *tray=(ll_tray*)((unsigned char*)g_shm+LL_TRAY_OFF);
    __builtin_memset(tray,0,sizeof(*tray));tray->magic=LL_TRAY_MAGIC;
    ll_native *native=(ll_native*)((unsigned char*)g_shm+LL_NATIVE_OFF);
    __builtin_memset(native,0,sizeof(*native));native->magic=LL_NATIVE_MAGIC;
    g_shm->magic = LL_MAGIC; g_shm->version = LL_VERSION; g_shm->pid = GetCurrentProcessId();
    logf("shm mapped at %p", g_shm);
}

static void publish(HWND hwnd, const POINT *dst, const SIZE *size, HDC src, const POINT *srcpt) {
    if (hwnd && hwnd != g_layered) hide_layered(hwnd);
    if(ll_pump)ll_pump();
    if(g_shm && g_shm->shutdown_request && hwnd) {g_shm->shutdown_request=0;PostMessageW(hwnd,WM_CLOSE,0,0);}
    if (dst) g_lastPos = *dst;
    if (size) g_lastSize = *size;
    if (!g_shm || !src) { if (g_shm && (dst||size)) { g_shm->x=g_lastPos.x; g_shm->y=g_lastPos.y; InterlockedIncrement((LONG*)&g_shm->seq);} return; }
    HBITMAP bmp = (HBITMAP)GetCurrentObject(src, OBJ_BITMAP);
    DIBSECTION ds; if (!bmp || GetObjectA(bmp, sizeof ds, &ds) != sizeof ds || !ds.dsBm.bmBits) { g_skipped++; return; }
    int w = g_lastSize.cx, h = g_lastSize.cy;
    if (w <= 0 || h <= 0 || w > LL_MAX_W || h > LL_MAX_H) { g_skipped++; return; }
    int sx = srcpt ? srcpt->x : 0, sy = srcpt ? srcpt->y : 0;
    int bw = ds.dsBm.bmWidth, bh = ds.dsBm.bmHeight, pitch = ds.dsBm.bmWidthBytes;
    int topdown = !g_flip;
    if (sx + w > bw) w = bw - sx; if (sy + h > bh) h = bh - sy;
    if (w <= 0 || h <= 0) { g_skipped++; return; }
    int idx=-1;uint32_t cur=g_shm->cur;
    for(int i=1;i<=LL_BUFFERS;i++) {
        int candidate=(cur+i)%LL_BUFFERS;if(candidate==(int)cur)continue;
        if(InterlockedCompareExchange((LONG*)&g_shm->buffer_state[candidate],1,0)==0){idx=candidate;break;}
    }
    if(idx<0){g_skipped++;return;}
    unsigned char *out = g_buf[idx];
    for (int r = 0; r < h; r++) {
        int srow = topdown ? (sy + r) : (bh - 1 - (sy + r));
        const unsigned char *in = (const unsigned char*)ds.dsBm.bmBits + (size_t)srow * pitch + (size_t)sx * 4;
        __builtin_memcpy(out + (size_t)r * w * 4, in, (size_t)w * 4);
    }
    g_shm->frame_x[idx] = g_lastPos.x; g_shm->frame_y[idx] = g_lastPos.y; g_shm->frame_w[idx] = w; g_shm->frame_h[idx] = h;
    g_shm->x = g_lastPos.x; g_shm->y = g_lastPos.y; g_shm->w = w; g_shm->h = h; g_shm->stride = w * 4;
    g_shm->hwnd = (uint64_t)(uintptr_t)hwnd;
    MemoryBarrier();
    InterlockedExchange((LONG*)&g_shm->buffer_state[idx],0);
    InterlockedExchange((LONG*)&g_shm->cur,idx);
    InterlockedIncrement((LONG*)&g_shm->seq);
    g_frames++;

}


static WNDPROC g_origProc;
static LRESULT CALLBACK sub_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_WINDOWPOSCHANGING && l) { ((WINDOWPOS*)l)->flags &= ~SWP_SHOWWINDOW; ((WINDOWPOS*)l)->flags |= SWP_HIDEWINDOW; }
    return CallWindowProcW(g_origProc, h, m, w, l);
}
static void hide_layered(HWND h) {
    char cls[64] = {0}; GetClassNameA(h, cls, sizeof cls);
    logf("layered hwnd %p class=%s visible=%d -> hiding Wine window", h, cls, IsWindowVisible(h));
    g_layered = h;
    HANDLE xw = GetPropA(h, "__wine_x11_whole_window");
    if (g_shm) g_shm->x11_window = (uint64_t)(uintptr_t)xw;
    logf("  x11 whole_window=0x%lx", (unsigned long)(uintptr_t)xw);
    // Default: keep the Win32 window visible (Unity stops rendering when hidden); the host makes the X11 window
    // invisible + click-through instead. LILITH_LINUX_HIDEWINDOW=1 restores the old experiment.
    if (!GetEnvironmentVariableA("LILITH_LINUX_HIDEWINDOW", NULL, 0)) return;
    g_origProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)sub_proc);
    ShowWindow(h, SW_HIDE);
    logf("  after hide visible=%d", IsWindowVisible(h));
}


/* ================= DXGI Present suppression =================
 * On Windows a WS_EX_LAYERED window shows only UpdateLayeredWindow content and DWM ignores swapchain presents.
 * Under Wine/DXVK the presents go to the X11 window (black box + Xwayland load). We keep the Win32 window
 * hidden and turn Present into a no-op once the layered stream is running. Unity keeps rendering because
 * Present still reports S_OK (not DXGI_STATUS_OCCLUDED). */
typedef HRESULT (WINAPI *PresentFn)(void*, UINT, UINT);
typedef HRESULT (WINAPI *Present1Fn)(void*, UINT, UINT, const void*);
static PresentFn o_Present; static Present1Fn o_Present1; static LONG g_presSkip, g_presPass;
static int g_suppress = 1;
static HRESULT WINAPI my_Present(void *sc, UINT sync, UINT flags) {
    if (g_layered && g_suppress) { g_presSkip++; if (!(flags & 1) && sync) Sleep(0); return S_OK; }
    g_presPass++; return o_Present(sc, sync, flags);
}
static HRESULT WINAPI my_Present1(void *sc, UINT sync, UINT flags, const void *pp) {
    if (g_layered && g_suppress) { g_presSkip++; return S_OK; }
    g_presPass++; return o_Present1(sc, sync, flags, pp);
}
static void patch_vtbl(void **slot, void *fn, void **orig) {
    DWORD old; if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { logf("vtbl protect failed"); return; }
    *orig = *slot; *slot = fn; VirtualProtect(slot, sizeof(void*), old, &old);
}
typedef struct { UINT w, h; struct { UINT n, d; } rr; UINT fmt, so, sc; } MODE_;
typedef struct { MODE_ bd; struct { UINT c, q; } sd; UINT usage, count; HWND wnd; BOOL windowed; UINT effect, flags; } SCDESC_;
typedef HRESULT (WINAPI *CreateDevSC)(void*, UINT, HMODULE, UINT, const void*, UINT, UINT, const SCDESC_*, void**, void**, void*, void**);
static DWORD WINAPI present_hook_thread(LPVOID arg) {
    HMODULE d3d = LoadLibraryA("d3d11.dll");
    CreateDevSC create = d3d ? (CreateDevSC)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (!create) { logf("present hook: no d3d11"); return 0; }
    HWND w = CreateWindowExA(0, "STATIC", "ll-dummy", WS_POPUP, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    SCDESC_ d = {0}; d.bd.w = 16; d.bd.h = 16; d.bd.fmt = 28 /*R8G8B8A8_UNORM*/; d.sd.c = 1; d.usage = 0x20 /*RENDER_TARGET_OUTPUT*/; d.count = 1; d.wnd = w; d.windowed = TRUE;
    void *sc = NULL, *dev = NULL, *ctx = NULL;
    HRESULT hr = create(NULL, 1 /*HARDWARE*/, NULL, 0, NULL, 0, 7 /*SDK*/, &d, &sc, &dev, NULL, &ctx);
    if (FAILED(hr) || !sc) { logf("present hook: dummy swapchain failed %08lx", hr); return 0; }
    void **vtbl = *(void***)sc;
    patch_vtbl(&vtbl[8], (void*)my_Present, (void**)&o_Present);
    // Present1 lives in the same vtable for DXVK (IDXGISwapChain4 single inheritance)
    patch_vtbl(&vtbl[22], (void*)my_Present1, (void**)&o_Present1);
    logf("present hook installed vtbl=%p Present=%p Present1=%p", vtbl, o_Present, o_Present1);
    // keep the dummy objects alive (module refcount); destroy only the window
    DestroyWindow(w);
    return 0;
}

static BOOL WINAPI my_ULW(HWND hwnd, HDC hdcDst, POINT *pptDst, SIZE *psize, HDC hdcSrc, POINT *pptSrc, COLORREF key, BLENDFUNCTION *bl, DWORD fl) {
    publish(hwnd, pptDst, psize, hdcSrc, pptSrc); return TRUE;
}
static BOOL WINAPI my_ULWI(HWND hwnd, const UPDATELAYEREDWINDOWINFO *i) {
    if (i) publish(hwnd, i->pptDst, i->psize, i->hdcSrc, i->pptSrc); return TRUE;
}

static int patch_jmp(void *target, void *dst) {
    unsigned char p[14] = {0xFF,0x25,0,0,0,0};
    __builtin_memcpy(p + 6, &dst, 8);
    DWORD old; if (!VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &old)) return 0;
    __builtin_memcpy(target, p, 14);
    VirtualProtect(target, 14, old, &old); FlushInstructionCache(GetCurrentProcess(), target, 14); return 1;
}

static void install_frame_hooks(void) {
    HMODULE u = GetModuleHandleA("user32.dll"); if (!u) u = LoadLibraryA("user32.dll");
    void *a = (void*)GetProcAddress(u, "UpdateLayeredWindow"), *b = (void*)GetProcAddress(u, "UpdateLayeredWindowIndirect");
    logf("hook ULW %p:%d ULWI %p:%d", a, a ? patch_jmp(a, (void*)my_ULW) : 0, b, b ? patch_jmp(b, (void*)my_ULWI) : 0);
}

static DWORD WINAPI stats_thread(LPVOID arg) {
    for (;;) { Sleep(10000); logf("stats: presents skip=%ld pass=%ld frames=%ld skipped=%ld visible=%d pos=(%ld,%ld) size=%ldx%ld", g_presSkip, g_presPass, g_frames, g_skipped, g_layered ? IsWindowVisible(g_layered) : -1, g_lastPos.x, g_lastPos.y, g_lastSize.cx, g_lastSize.cy); }
}

static void load_real(void) {
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); lstrcatA(path, "\\version.dll");
    HMODULE h = LoadLibraryA(path);
#define RES(n) p_##n = (void*)GetProcAddress(h, #n);
    EXPORTS(RES)
}

typedef void* (*fn_v)(void);
typedef void* (*fn_p)(void*);
typedef void** (*fn_asm)(void*, size_t*);
typedef const char* (*fn_name)(void*);
typedef void* (*fn_cls)(void*, const char*, const char*);
typedef void* (*fn_meth)(void*, const char*, int);

static const char *probe[][3] = {
    {"LayeredWindowPresenter","UlwFull","4"}, {"LayeredWindowPresenter","UlwDirty","4"},
    {"LayeredWindowPresenter","OnReadback","2"}, {"LayeredWindowPresenter","ApplyWindowSize","2"},
    {"LayeredWindowPresenter","ApplyPassThrough","1"}, {"LayeredWindowPresenter","SetKeyboardInputActive","1"},
    {"LayeredWindowPresenter","ReassertTopMost","0"}, {"LayeredWindowPresenter","GrabForeground","0"},
    {"TransparentWindowNew","BeginKeyboardInput","0"}, {"HotkeyService","Tick","0"},
    {"HotkeyService","IsKeyDown","1"}, {"ActiveWindowCollider","UpdateCurrentOpenedWindows","0"},
    {"GameForegroundDetector","ComputeGaming","0"}, {"ExternalAudioMonitor","SampleFrame","0"},
    {"DesktopIconCounter","TryGetCount","1"},
};


/* ================= native input bridge (stage 3) ================= */
static int (*ll_frame_count)(void);
static int (*ll_get_fps)(void);static void (*ll_set_fps)(int);static int ll_fps_cap=60;
static unsigned char (*ll_keyboard_active)(void *);
static unsigned char (*ll_force_passthrough)(void *);
static int ll_input_frame = -1;
static uint32_t ll_down_seen[3], ll_up_seen[3], ll_down_frame, ll_up_frame;
static void ll_tick_input(void) {
    int frame = ll_frame_count ? ll_frame_count() : (int)(GetTickCount()/16);
    if (frame == ll_input_frame || !g_shm) return;
    ll_input_frame = frame; ll_down_frame = ll_up_frame = 0;
    for (int i=0;i<3;i++) {
        uint32_t d=g_shm->mouse_down[i], u=g_shm->mouse_up[i];
        if(d != ll_down_seen[i]) ll_down_frame |= 1u<<i;
        if(u != ll_up_seen[i]) ll_up_frame |= 1u<<i;
        ll_down_seen[i]=d; ll_up_seen[i]=u;
    }
    g_shm->input_frames++;
    uint32_t flags=0;
    if(ll_keyboard_active && ll_keyboard_active(NULL)) flags |= LL_KEYBOARD;
    if(ll_force_passthrough && ll_force_passthrough(NULL)) flags |= LL_PASSTHROUGH;
    g_shm->game_flags=flags;
}
static unsigned char ll_mouse(int b, void *m) { ll_tick_input(); return g_shm && b>=0 && b<3 && ((g_shm->mouse_buttons>>b)&1); }
static unsigned char ll_mouse_down(int b, void *m) { ll_tick_input(); return b>=0 && b<3 && ((ll_down_frame>>b)&1); }
static unsigned char ll_mouse_up(int b, void *m) { ll_tick_input(); return b>=0 && b<3 && ((ll_up_frame>>b)&1); }
static BOOL ll_cursor(POINT *p, void *m) {
    if(!p || !g_shm) return FALSE;
    p->x=g_shm->mouse_x; p->y=g_shm->mouse_y; return TRUE;
}
typedef struct { float x,y,z; } ll_vec3;
static ll_vec3 *ll_position(ll_vec3 *p, void *m) {
    ll_tick_input();
    p->x=g_shm ? (float)(g_shm->mouse_x-g_lastPos.x) : -10000;
    p->y=g_shm ? (float)(g_lastSize.cy-(g_shm->mouse_y-g_lastPos.y)) : -10000;
    p->z=0; return p;
}

// New Input System uses explicit native events, independent of Wine window focus.
#pragma pack(push, 1)
typedef struct { uint32_t type; uint16_t size, device; double time; uint32_t id; } ll_native_event;
typedef struct { ll_native_event event; uint32_t format; float x,y,dx,dy,sx,sy; uint16_t buttons,display,clicks,pad; } ll_mouse_event;
typedef struct { ll_native_event event; uint32_t format; unsigned char keys[14]; } ll_key_event;
typedef struct { ll_native_event event; uint32_t character; } ll_text_event;
#pragma pack(pop)
static void (*ll_queue_native)(void *);
static double (*ll_native_time)(void);
static void *(*ll_mouse_current)(void *), *(*ll_key_current)(void *);
static int (*ll_device_id)(void *, void *);
static uint32_t ll_mouse_format, ll_key_format;
static unsigned char ll_keys[14];
static int ll_vk_to_key(uint32_t k) {
    if(k>=65 && k<=90) return 15+k-65;
    if(k>=49 && k<=57) return 41+k-49;
    if(k>=0x70 && k<=0x7b) return 94+k-0x70;
    switch(k) {
    case 48:return 50; case 32:return 1;case 13:return 2;case 9:return 3;
    case 16:return 51;case 17:return 55;case 18:return 53;
    case 27:return 60;case 37:return 61;case 39:return 62;case 38:return 63;case 40:return 64;
    case 8:return 65;case 34:return 66;case 33:return 67;case 36:return 68;case 35:return 69;
    case 45:return 70;case 46:return 71;default:return 0;
    }
}

/* UTF-16 goes directly into Unity Event objects: no ANSI Windows message conversion. */
static unsigned char (*ll_native_pop)(void *);
static void (*ll_ev_type)(void *,int),(*ll_ev_key)(void *,int),(*ll_ev_mods)(void *,int);
static void (*ll_ev_char)(void *,uint16_t);
typedef struct {int type,key,mods;uint16_t ch;} ll_gui;
static ll_gui ll_gui_queue[256];static unsigned ll_gui_rd,ll_gui_wr;
static void ll_gui_push(int type,int key,uint16_t ch,int mods) {
    if(ll_gui_wr-ll_gui_rd>=256){++g_shm->event_dropped;return;}
    ll_gui_queue[ll_gui_wr++%256]=(ll_gui){type,key,mods,ch};
}
static unsigned char ll_pop_event(void *ev,void *method) {
    if(ll_gui_rd!=ll_gui_wr) {
        ll_gui e=ll_gui_queue[ll_gui_rd++%256];
        ll_ev_type(ev,e.type);ll_ev_key(ev,e.key);ll_ev_char(ev,e.ch);ll_ev_mods(ev,e.mods);return 1;
    }
    return ll_native_pop?ll_native_pop(ev):0;
}
static int ll_unity_key(uint32_t vk) {
    switch(vk){case 8:return 8;case 9:return 9;case 13:return 13;case 27:return 27;case 46:return 127;
    case 38:return 273;case 40:return 274;case 39:return 275;case 37:return 276;case 45:return 277;
    case 36:return 278;case 35:return 279;case 33:return 280;case 34:return 281;default:return 0;}
}
static int32_t *ll_screen_at(int x,int y) {
    if(!g_shm || !g_shm->screen_count)return NULL;
    unsigned n=g_shm->screen_count;if(n>8)n=8;
    for(unsigned i=0;i<n;i++){int32_t *s=g_shm->screen_rects[i];if(x>=s[0] && x<s[0]+s[2] && y>=s[1] && y<s[1]+s[3])return s;}
    return g_shm->screen_rects[0];
}
typedef struct {int x,y,w,h;} ll_rectint;
static unsigned char ll_workarea(void *self,ll_rectint *area,void *method) {
    int32_t *s=ll_screen_at(g_lastPos.x+g_lastSize.cx/2,g_lastPos.y+g_lastSize.cy/2);
    if(!s || !area || s[6]<=0 || s[7]<=0)return 0;*area=(ll_rectint){s[4],s[5],s[6],s[7]};return 1;
}

/* Build the game's existing typed List<MonitorInfo> using runtime metadata. */
static void *ll_monitor_list_class,*ll_monitor_value_class;
static void *(*ll_object_new)(void *),*(*ll_array_new)(void *,size_t);
static void (*ll_object_init)(void *),(*ll_write_barrier)(void *,void **,void *);
static int ll_list_items_off,ll_list_size_off,ll_list_version_off;
typedef struct {float x,y,w,h;unsigned char primary,pad[3];float wx,wy,ww,wh;} ll_monitor_value;
static void *ll_get_monitors(void *self,void *method) {
    void *list=ll_object_new(ll_monitor_list_class);if(!list)return NULL;
    ll_object_init(list);
    unsigned n=g_shm?g_shm->screen_count:0;if(n>8)n=8;
    unsigned count=n?n:1;
    void *array=ll_array_new(ll_monitor_value_class,count);if(!array)return list;
    ll_monitor_value *values=(ll_monitor_value*)((unsigned char*)array+4*sizeof(void*));
    for(unsigned i=0;i<count;i++) {
        int fallback[8]={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
        int32_t *r=n?g_shm->screen_rects[i]:fallback;
        values[i]=(ll_monitor_value){(float)r[0],(float)r[1],(float)r[2],(float)r[3],i==0,{0,0,0},(float)r[4],(float)r[5],(float)r[6],(float)r[7]};
    }
    ll_write_barrier(list,(void**)((unsigned char*)list+ll_list_items_off),array);
    *(int*)((unsigned char*)list+ll_list_size_off)=count;
    *(int*)((unsigned char*)list+ll_list_version_off)=1;
    static int logged;if(!logged){logf("native monitor list count=%d bounds=%dx%d work=%dx%d",count,(int)values[0].w,(int)values[0].h,(int)values[0].ww,(int)values[0].wh);logged=1;}
    return list;
}

/* KDE obstacles are opaque stable bridge IDs, not Win32 HWNDs. */
static void *ll_window_list_class,*ll_window_value_class;
static void *(*ll_string_new)(const char *);
typedef struct {uint64_t handle;void *title,*process;ll_rectint bounds;unsigned char maximized,pad[7];} ll_desktop_window;
static unsigned ll_read_raw_windows(ll_window *out) {
    if(!g_shm)return 0;
    for(int attempt=0;attempt<4;attempt++) {
        uint32_t seq=g_shm->windows_seq;if(seq&1)continue;MemoryBarrier();
        unsigned n=g_shm->window_count;if(n>LL_WINDOW_CAP)n=LL_WINDOW_CAP;
        __builtin_memcpy(out,g_shm->windows,n*sizeof(ll_window));MemoryBarrier();
        if(seq==g_shm->windows_seq)return n;
    }return 0;
}

static ll_motion_guard ll_window_motion;
static DWORD ll_last_window_burst;
static int ll_motion_enabled=1;
static unsigned ll_read_windows(ll_window *out) {
    static ll_window filtered[LL_WINDOW_CAP];static unsigned count;static uint32_t lastSeq=~0u;
    if(!g_shm)return 0;
    uint32_t seq=g_shm->windows_seq;
    if(!(seq&1) && seq!=lastSeq) {
        ll_window raw[LL_WINDOW_CAP];unsigned n=ll_read_raw_windows(raw);
        MemoryBarrier();
        if(seq==g_shm->windows_seq) {
            unsigned before=ll_window_motion.events;
            if(ll_motion_enabled)count=ll_motion_update(&ll_window_motion,raw,n,GetTickCount(),filtered);
            else {count=n;__builtin_memcpy(filtered,raw,n*sizeof(ll_window));}
            lastSeq=seq;
            if(ll_window_motion.events!=before) {
                DWORD now=GetTickCount();
                if(now-ll_last_window_burst>500)logf("KWin motion guard: abrupt support detached; events=%u cooldown=%u ms",ll_window_motion.events,ll_window_motion.settle_ms);
                ll_last_window_burst=now;
            }
        }
    }
    __builtin_memcpy(out,filtered,count*sizeof(ll_window));return count;
}
static void *ll_get_windows(void *self,void *exclude,void *method) {
    ll_window snapshot[LL_WINDOW_CAP];unsigned count=ll_read_windows(snapshot);
    void *list=ll_object_new(ll_window_list_class);if(!list)return NULL;ll_object_init(list);
    void *array=ll_array_new(ll_window_value_class,count);if(!array)return list;
    ll_desktop_window *values=(void*)((unsigned char*)array+4*sizeof(void*));
    void *title=ll_string_new("KDE window"),*process=ll_string_new("kwin");
    for(unsigned i=0;i<count;i++) {
        ll_window w=snapshot[i];values[i].handle=w.id;values[i].bounds=(ll_rectint){w.x,w.y,w.w,w.h};values[i].maximized=(w.flags&1)!=0;
        ll_write_barrier(array,&values[i].title,title);ll_write_barrier(array,&values[i].process,process);
    }
    ll_write_barrier(list,(void**)((unsigned char*)list+ll_list_items_off),array);
    *(int*)((unsigned char*)list+ll_list_size_off)=count;*(int*)((unsigned char*)list+ll_list_version_off)=1;
    static int old=-1;if(old!=(int)count){old=count;logf("KWin game obstacle list: %d windows",count);}
    return list;
}
static unsigned char ll_live_window(void *self,void *handle,ll_rectint *rect,void *method) {
    if(!rect)return 0;ll_window snapshot[LL_WINDOW_CAP];unsigned n=ll_read_windows(snapshot);
    for(unsigned i=0;i<n;i++)if(snapshot[i].id==(uint64_t)(uintptr_t)handle) {
        ll_window w=snapshot[i];*rect=(ll_rectint){w.x,w.y,w.w,w.h};return 1;
    }
    __builtin_memset(rect,0,sizeof(*rect));return 0;
}
/* Unified desktop mapping, including cropped layered-window fallback. */
static unsigned char (*ll_map_ready)(void *);
static ll_vec3 *(*ll_desktop_world)(ll_vec3 *,float,float,float,void *);
static uint64_t (*ll_world_desktop)(ll_vec3 *,void *);
static void *(*ll_get_cam)(void *,void *);
static ll_vec3 *(*ll_screen_world)(ll_vec3 *,void *,ll_vec3 *,void *);
static ll_vec3 *(*ll_world_screen)(ll_vec3 *,void *,ll_vec3 *,void *);
static ll_vec3 *ll_collider_to_world(ll_vec3 *out,void *self,float x,float y,void *method) {
    if(ll_map_ready(NULL))return ll_desktop_world(out,x,y,0,NULL);
    void *camera=ll_get_cam(self,NULL);if(!camera){__builtin_memset(out,0,sizeof(*out));return out;}
    ll_vec3 screen={x-g_lastPos.x,g_lastSize.cy-(y-g_lastPos.y),0};
    ll_screen_world(out,camera,&screen,NULL);out->z=0;return out;
}
static unsigned char ll_collider_to_desktop(void *self,ll_vec3 *world,int *x,int *y,void *method) {
    if(!world || !x || !y)return 0;float px,py;
    if(ll_map_ready(NULL)) {
        union {uint64_t bits;struct {float x,y;} value;} p;
        p.bits=ll_world_desktop(world,NULL);px=p.value.x;py=p.value.y;
    } else {
        void *camera=ll_get_cam(self,NULL);if(!camera)return 0;
        ll_vec3 screen;ll_world_screen(&screen,camera,world,NULL);
        px=screen.x+g_lastPos.x;py=g_lastPos.y+g_lastSize.cy-screen.y;
    }
    if(px!=px || py!=py || px<-100000 || px>100000 || py<-100000 || py>100000)return 0;
    *x=(int)(px+(px>=0?0.5f:-0.5f));*y=(int)(py+(py>=0?0.5f:-0.5f));return 1;
}

static void *ll_collider_instance_field,*ll_collider_count_method,*ll_collider_enabled_method;
static void (*ll_static_field_get)(void *,void *);
static int ll_active_windows_off,ll_window_timer_off;
static void *ll_character_field,*ll_hidden_method;static int ll_character_rb_off;
static void *(*ll_object_class)(void *);
static fn_meth ll_runtime_method;
static void ll_motion_telemetry(void) {
    static DWORD last;DWORD now=GetTickCount();if(now-last<250)return;last=now;
    if(!ll_character_field || !ll_static_field_get)return;
    void *character=NULL;ll_static_field_get(ll_character_field,&character);if(!character)return;
    int hidden=ll_hidden_method?((unsigned char(*)(void*,void*))*(void**)ll_hidden_method)(character,ll_hidden_method):0;
    static int oldHidden=-1;
    if(hidden!=oldHidden){oldHidden=hidden;logf("window physics: hidden=%d recentWindowBurst=%d",hidden,ll_last_window_burst && now-ll_last_window_burst<3000);}
    static DWORD burstLogged;
    if(ll_last_window_burst && ll_last_window_burst!=burstLogged && ll_character_rb_off && ll_object_class && ll_runtime_method) {
        burstLogged=ll_last_window_burst;
        void *rb=*(void**)((unsigned char*)character+ll_character_rb_off);if(!rb)return;
        void *cls=ll_object_class(rb),*method=ll_runtime_method(cls,"get_velocity",0);
        if(method){union{uint64_t bits;struct{float x,y;}v;} value;value.bits=((uint64_t(*)(void*,void*))*(void**)method)(rb,method);
            if(value.v.x==value.v.x && value.v.y==value.v.y && value.v.x>-10000 && value.v.x<10000 && value.v.y>-10000 && value.v.y<10000)
                logf("window physics: velocity x100=(%ld,%ld)",(long)(value.v.x*100),(long)(value.v.y*100));}
    }
}
/* Window-origin rebounds use the game's own throw cap, not a global velocity clamp. */
typedef struct {ll_vec3 center,extents;} ll_bounds;
static void *ll_rebound_drag_method,*ll_rebound_bounds_method,*ll_rebound_height_method;
static void *ll_rebound_handler_field,*ll_rebound_clamp_method;
static int ll_rebound_timer_off,ll_rebound_still_off,ll_rebound_enabled=1;
static void ll_window_rebound(void) {
    if(!ll_rebound_enabled || !ll_character_field || !ll_static_field_get || !ll_rebound_timer_off || !ll_rebound_clamp_method)return;
    void *character=NULL,*collider=NULL,*handler=NULL;
    ll_static_field_get(ll_character_field,&character);ll_static_field_get(ll_collider_instance_field,&collider);
    ll_static_field_get(ll_rebound_handler_field,&handler);
    if(!character || !collider || !handler)return;
    void *rb=*(void**)((unsigned char*)character+ll_character_rb_off);if(!rb)return;
    static void *bodyClass,*velocityMethod;
    void *bc=ll_object_class(rb);if(bc!=bodyClass){bodyClass=bc;velocityMethod=ll_runtime_method(bc,"get_velocity",0);}
    if(!velocityMethod || !*(void**)velocityMethod)return;
    union {uint64_t bits;struct {float x,y;}v;} velocity;
    velocity.bits=((uint64_t(*)(void*,void*))*(void**)velocityMethod)(rb,velocityMethod);
    DWORD now=GetTickCount();static void *lastBody;static float lastVy;static DWORD lastSample,lastHand,lastContact;
    static uint64_t contactWindow,episodeWindow;static DWORD episodeUntil;
    if(lastBody!=rb || !lastSample || now-lastSample>250){lastBody=rb;lastVy=velocity.v.y;lastSample=now;lastContact=0;episodeWindow=0;return;}
    float previous=lastVy;lastVy=velocity.v.y;lastSample=now;
    int hand=((unsigned char(*)(void*,void*))*(void**)ll_rebound_drag_method)(character,ll_rebound_drag_method);
    int hidden=((unsigned char(*)(void*,void*))*(void**)ll_hidden_method)(character,ll_hidden_method);
    if(hand || hidden){if(hand)lastHand=now;lastContact=0;episodeWindow=0;return;}
    float height=((float(*)(void*,void*))*(void**)ll_rebound_height_method)(character,ll_rebound_height_method);
    ll_bounds bounds;
    if(((unsigned char(*)(void*,void*,void*))*(void**)ll_rebound_bounds_method)(character,&bounds,ll_rebound_bounds_method)) {
        ll_vec3 foot={bounds.center.x,bounds.center.y-bounds.extents.y,0},head={bounds.center.x,bounds.center.y+bounds.extents.y,0};
        int fx,fy,hx,hy;
        if(ll_collider_to_desktop(collider,&foot,&fx,&fy,NULL) && ll_collider_to_desktop(collider,&head,&hx,&hy,NULL)) {
            int display=0;unsigned screens=g_shm->screen_count;if(screens>8)screens=8;
            for(unsigned i=0;i<screens;i++){int32_t *r=g_shm->screen_rects[i];if(r[2]>0 && r[3]>0 && fx>=r[0] && fx<r[0]+r[2] && fy>=r[1]-8 && fy<r[1]+r[3]+8){display=1;break;}}
            int pixels=fy-hy;if(pixels<0)pixels=-pixels;int tolerance=pixels/6;if(tolerance<12)tolerance=12;if(tolerance>80)tolerance=80;
            if(display)for(unsigned i=0;i<ll_window_motion.count;i++) {
                ll_motion_item item=ll_window_motion.items[i];ll_window w=item.window;
                int active=(item.moved && now-item.moved<=500) || (w.id==episodeWindow && (int32_t)(episodeUntil-now)>0);
                int gap=fy-w.y;if(gap<0)gap=-gap;
                if(active && !(w.flags&1) && fx>=w.x && fx<w.x+w.w && gap<=tolerance){lastContact=now;contactWindow=w.id;break;}
            }
        }
    }
    if(!ll_rebound_candidate(previous,velocity.v.y,height,now,lastContact,lastHand))return;
    ((void(*)(void*,void*,void*))*(void**)ll_rebound_clamp_method)(handler,rb,ll_rebound_clamp_method);
    *(float*)((unsigned char*)character+ll_rebound_timer_off)=0;
    if(ll_rebound_still_off)*(float*)((unsigned char*)character+ll_rebound_still_off)=0;
    episodeWindow=contactWindow;episodeUntil=now+4000;
    union {uint64_t bits;struct{float x,y;}v;} after;after.bits=((uint64_t(*)(void*,void*))*(void**)velocityMethod)(rb,velocityMethod);lastVy=after.v.y;
    static DWORD lastLog;if(now-lastLog>200){lastLog=now;
        if(velocity.v.y<10000 && after.v.y>-10000 && after.v.y<10000)logf("window rebound: original throw cap applied, vy x100=%ld -> %ld; landing watchdog rearmed",(long)(velocity.v.y*100),(long)(after.v.y*100));}
}

static void ll_sync_window_bounds(void) {
    if(!ll_static_field_get || !ll_collider_instance_field || !ll_active_windows_off)return;
    void *instance=NULL;ll_static_field_get(ll_collider_instance_field,&instance);if(!instance)return;
    ll_window snapshot[LL_WINDOW_CAP];unsigned n=ll_read_windows(snapshot);
    unsigned hash=2166136261u;const unsigned char *bytes=(const unsigned char*)snapshot;
    for(unsigned i=0;i<n*sizeof(ll_window);i++)hash=(hash^bytes[i])*16777619u;
    hash^=n;static unsigned lastHash;
    if(hash!=lastHash){lastHash=hash;if(ll_window_timer_off)*(float*)((unsigned char*)instance+ll_window_timer_off)=0;}
    void *list=*(void**)((unsigned char*)instance+ll_active_windows_off);if(!list)return;
    int count=*(int*)((unsigned char*)list+ll_list_size_off);if(count<0 || count>LL_WINDOW_CAP)return;
    void *array=*(void**)((unsigned char*)list+ll_list_items_off);if(!array)return;
    ll_desktop_window *items=(void*)((unsigned char*)array+4*sizeof(void*));
    for(int i=0;i<count;i++) {
        unsigned j;for(j=0;j<n;j++)if(snapshot[j].id==items[i].handle)break;
        if(j==n){items[i].bounds=(ll_rectint){0,0,0,0};items[i].maximized=1;continue;}
        ll_window w=snapshot[j];items[i].bounds=(ll_rectint){w.x,w.y,w.w,w.h};items[i].maximized=(w.flags&1)!=0;
    }
}
static void ll_collider_diagnostics(void) {
    static DWORD previous;DWORD now=GetTickCount();if(now-previous<1000)return;previous=now;
    if(!ll_collider_instance_field || !ll_static_field_get)return;
    void *instance=NULL;ll_static_field_get(ll_collider_instance_field,&instance);if(!instance)return;
    int count=ll_collider_count_method?((int(*)(void*,void*))*(void**)ll_collider_count_method)(instance,ll_collider_count_method):-1;
    int enabled=ll_collider_enabled_method?((unsigned char(*)(void*,void*))*(void**)ll_collider_enabled_method)(instance,ll_collider_enabled_method):-1;
    static int lastCount=-2,lastEnabled=-2;
    if(count!=lastCount || enabled!=lastEnabled){lastCount=count;lastEnabled=enabled;logf("KWin collider status enabled=%d activeSlots=%d",enabled,count);}
}
/* Native global shortcuts keep the original action handlers and saved bindings. */
static uint64_t (*ll_hotkey_combo)(int,void*);
static unsigned char (*ll_hotkey_capturing)(int,void*);
static void (*ll_hotkey_invoke)(int,void*);
static int ll_hotkey_enabled;
static uint32_t ll_hotkey_seen[LL_HOTKEY_COUNT];
static void ll_hotkey_no_wine_fire(void *method) {(void)method;}
static unsigned char ll_hotkey_key_down(int vk,void *method) {
    (void)method;
    return g_shm && g_shm->host_alive && vk>0 && vk<256 &&
        ((g_shm->focused_vk[vk/32]>>(vk%32))&1);
}
static int ll_hotkey_mods(int *strict,void *method) {
    int mods=(ll_hotkey_key_down(17,NULL)?1:0)|(ll_hotkey_key_down(18,NULL)?2:0)|
        (ll_hotkey_key_down(16,NULL)?4:0)|(ll_hotkey_key_down(91,NULL)?8:0);
    if(strict)*strict=mods;(void)method;return mods;
}
static unsigned char ll_hotkey_cancel(void *method) {return ll_hotkey_key_down(27,method);}
static unsigned char ll_hotkey_clear(void *method) {return ll_hotkey_key_down(8,method)||ll_hotkey_key_down(46,method);}
static void ll_hotkey_pump(int host_ok) {
    if(!ll_hotkey_enabled) return;
    uint32_t mods[LL_HOTKEY_COUNT],keys[LL_HOTKEY_COUNT],capturing=0;
    int changed=!g_shm->hotkey_ready;
    for(int i=0;i<LL_HOTKEY_COUNT;i++) {
        uint64_t combo=ll_hotkey_combo(i,NULL);
        mods[i]=(uint32_t)combo;keys[i]=(uint32_t)(combo>>32);
        if(mods[i]!=g_shm->hotkey_mods[i] || keys[i]!=g_shm->hotkey_keys[i])changed=1;
        if(ll_hotkey_capturing(i,NULL))capturing=1;
    }
    g_shm->hotkey_capturing=capturing;
    if(changed) {
        InterlockedIncrement((LONG*)&g_shm->hotkey_seq);MemoryBarrier();
        for(int i=0;i<LL_HOTKEY_COUNT;i++) {
            g_shm->hotkey_mods[i]=mods[i];g_shm->hotkey_keys[i]=keys[i];
            logf("native hotkey binding action=%d mods=%u vk=%u",i,mods[i],keys[i]);
        }
        MemoryBarrier();InterlockedIncrement((LONG*)&g_shm->hotkey_seq);g_shm->hotkey_ready=1;
    }
    for(int i=0;i<LL_HOTKEY_COUNT;i++) {
        uint32_t fired=g_shm->hotkey_fired[i];
        if(fired!=ll_hotkey_seen[i]) {
            ll_hotkey_seen[i]=fired;
            if(host_ok && !capturing && g_shm->hotkey_status==1 && keys[i]) {
                logf("native hotkey invoke action=%d",i);ll_hotkey_invoke(i,NULL);
            }
        }
    }
}

static void ll_path_diagnostics(void);
static void ll_tray_pump(int host_ok);
static void ll_native_pump(int host_ok);
static void ll_pump_input(void) {
    ll_path_diagnostics();
    if(!g_shm) return;
    ll_sync_window_bounds();
    ll_window_rebound();
    ll_motion_telemetry();
    ll_collider_diagnostics();
    static DWORD fps_checked;DWORD tick=GetTickCount();
    if(ll_get_fps && ll_set_fps && tick-fps_checked>1000){fps_checked=tick;int fps=ll_get_fps();if(fps<=0 || fps>ll_fps_cap){ll_set_fps(ll_fps_cap);logf("FPS safety cap %d (was %d)",ll_fps_cap,fps);}}
    uint32_t flags=0;
    if(ll_keyboard_active && ll_keyboard_active(NULL)) flags |= LL_KEYBOARD;
    if(ll_force_passthrough && ll_force_passthrough(NULL)) flags |= LL_PASSTHROUGH;
    g_shm->game_flags=flags;
    // Called on Unity's frame-publishing thread, not a detached managed worker.
    static uint32_t heartbeat=0; static DWORD alive=0;
    if(g_shm->host_heartbeat!=heartbeat) {heartbeat=g_shm->host_heartbeat;alive=GetTickCount();}
    int host_ok = alive && GetTickCount()-alive<2000;
    ll_hotkey_pump(host_ok);
    ll_tray_pump(host_ok);
    ll_native_pump(host_ok);
    static uint32_t reset_seen;
    int reset=reset_seen!=g_shm->input_reset || !host_ok;
    reset_seen=g_shm->input_reset;
    if(reset) {
        uint32_t buttons=g_shm->mouse_buttons;g_shm->mouse_buttons=0;
        for(int i=0;i<3;i++)if(buttons&(1u<<i))++g_shm->mouse_up[i];
        ll_gui_rd=ll_gui_wr;g_shm->event_read=g_shm->event_write;
    }
    int mid=0,kid=0;
    if(ll_queue_native && ll_device_id) {
        void *mouse=ll_mouse_current ? ll_mouse_current(NULL) : NULL;
        void *keyboard=ll_key_current ? ll_key_current(NULL) : NULL;
        if(mouse) mid=ll_device_id(mouse,NULL);
        if(keyboard) kid=ll_device_id(keyboard,NULL);
    }
    double now=ll_native_time ? ll_native_time() : 0;
    float x=(float)(g_shm->mouse_x-g_lastPos.x), y=(float)(g_lastSize.cy-(g_shm->mouse_y-g_lastPos.y));
    static float px,py; static int logged;
    ll_mouse_event me={0};
    me.event=(ll_native_event){1398030676,sizeof(me),(uint16_t)mid,now,0};me.format=ll_mouse_format;
    me.x=x;me.y=y; me.dx=px?x-px:0;me.dy=py?y-py:0;px=x;py=y;
    me.buttons=host_ok ? g_shm->mouse_buttons : 0;
    if(mid) {
        ll_queue_native(&me);
        if(!logged) {logf("native input device mouse=%d keyboard=%d mousefmt=%08x keyfmt=%08x",mid,kid,ll_mouse_format,ll_key_format);logged=1;}
    }
    uint32_t rd=g_shm->event_read,wr=g_shm->event_write;
    while(rd!=wr) {
        MemoryBarrier();ll_event e=g_shm->events[rd%LL_EVENT_CAP];
        if(!host_ok || (e.type<=3 && !(flags&LL_KEYBOARD))) {++rd;continue;}
        if(e.type==1 || e.type==2) {
            int key=ll_vk_to_key(e.code);
            if(key>0 && key<112) {
                if(e.type==1) ll_keys[key/8]|=1u<<(key%8); else ll_keys[key/8]&=~(1u<<(key%8));
                ll_key_event ke={0};ke.event=(ll_native_event){1398030676,sizeof(ke),(uint16_t)kid,now,0};ke.format=ll_key_format;
                __builtin_memcpy(ke.keys,ll_keys,14);if(kid)ll_queue_native(&ke);
            }
        } else if(e.type==3) {
            // Single direct UTF-16 text route.
            if(ll_native_pop)ll_gui_push(4,0,(uint16_t)e.code,e.extra);
        } else if(e.type==4 && mid) {me.sy=(float)(int32_t)e.code;ll_queue_native(&me);}
        if((e.type==1 || e.type==2) && ll_native_pop) {
            int key=ll_unity_key(e.code);
            if(key)ll_gui_push(e.type==1?4:5,key,0,e.extra);
            else if((e.extra&2) && e.code>=65 && e.code<=90)ll_gui_push(e.type==1?4:5,e.code+32,0,e.extra);
        }
        ++rd;++g_shm->input_events;
    }
    MemoryBarrier();g_shm->event_read=rd;
    if(reset || !(flags&LL_KEYBOARD)) {
        int nonzero=0;for(int i=0;i<14;i++) nonzero|=ll_keys[i];
        if(nonzero && kid) {
            __builtin_memset(ll_keys,0,14);ll_key_event ke={0};ke.event=(ll_native_event){1398030676,sizeof(ke),(uint16_t)kid,now,0};ke.format=ll_key_format;ll_queue_native(&ke);
        }
    }
    ++g_shm->input_frames;
}
static void *ll_method(fn_cls cls, fn_meth meth, void *img, const char *ns, const char *cn, const char *mn, int argc) {
    void *c=cls(img,ns,cn); void *m=c ? meth(c,mn,argc) : NULL; return m ? *(void**)m : NULL;
}
static void ll_hook_method(fn_cls cls, fn_meth meth, void *img, const char *ns, const char *cn, const char *mn, int argc, void *replacement) {
    void *p=ll_method(cls,meth,img,ns,cn,mn,argc);
    logf("input hook %s.%s ptr=%p installed=%d",cn,mn,p,p ? patch_jmp(p,replacement) : 0);
}
#include "desktop.cfrag"
#include "path_display.cfrag"
#include "tray.cfrag"
#include "native.cfrag"

static void install_input(HMODULE ga, void *img, void *inputImg, void *systemImg, void *guiImg, void *coreImg, fn_cls cls, fn_meth meth) {
    typedef void *(*resolve_icall_fn)(const char *);
    resolve_icall_fn resolve=(resolve_icall_fn)GetProcAddress(ga,"il2cpp_resolve_icall");
    char cap[16]={0};GetEnvironmentVariableA("LILITH_LINUX_FPS",cap,sizeof(cap));int requested=0;
    for(int i=0;cap[i]>='0' && cap[i]<='9';i++)requested=requested*10+cap[i]-'0';
    if(requested>=15 && requested<=240)ll_fps_cap=requested;
    ll_get_fps=resolve?(void*)resolve("UnityEngine.Application::get_targetFrameRate"):NULL;
    ll_set_fps=resolve?(void*)resolve("UnityEngine.Application::set_targetFrameRate"):NULL;
    logf("FPS safety cap configured=%d get=%p set=%p",ll_fps_cap,ll_get_fps,ll_set_fps);
    ll_frame_count=resolve ? (void*)resolve("UnityEngine.Time::get_frameCount") : NULL;
    ll_keyboard_active=(void*)ll_method(cls,meth,img,"","TransparentWindowNew","get_IsKeyboardInputActive",0);
    ll_force_passthrough=(void*)ll_method(cls,meth,img,"","TransparentWindowNew","get_IsForceClickThrough",0);
    if(!ll_frame_count || !inputImg) { logf("input: prerequisites missing frame=%p image=%p",ll_frame_count,inputImg); return; }
    ll_hook_method(cls,meth,inputImg,"UnityEngine","Input","GetMouseButton",1,(void*)ll_mouse);
    ll_hook_method(cls,meth,inputImg,"UnityEngine","Input","GetMouseButtonDown",1,(void*)ll_mouse_down);
    ll_hook_method(cls,meth,inputImg,"UnityEngine","Input","GetMouseButtonUp",1,(void*)ll_mouse_up);
    ll_hook_method(cls,meth,inputImg,"UnityEngine","Input","get_mousePosition",0,(void*)ll_position);
    ll_hook_method(cls,meth,img,"","WindowsNativeAPI","GetCursorPos",1,(void*)ll_cursor);

    ll_queue_native=resolve ? (void*)resolve("UnityEngineInternal.Input.NativeInputSystem::QueueInputEvent") : NULL;
    ll_native_time=resolve ? (void*)resolve("UnityEngineInternal.Input.NativeInputSystem::get_currentTime") : NULL;
    if(systemImg) {
        ll_mouse_current=(void*)ll_method(cls,meth,systemImg,"UnityEngine.InputSystem","Mouse","get_current",0);
        ll_key_current=(void*)ll_method(cls,meth,systemImg,"UnityEngine.InputSystem","Keyboard","get_current",0);
        ll_device_id=(void*)ll_method(cls,meth,systemImg,"UnityEngine.InputSystem","InputDevice","get_deviceId",0);
        uint32_t (*mf)(void*)=(void*)ll_method(cls,meth,systemImg,"UnityEngine.InputSystem.LowLevel","MouseState","get_Format",0);
        uint32_t (*kf)(void*)=(void*)ll_method(cls,meth,systemImg,"UnityEngine.InputSystem.LowLevel","KeyboardState","get_Format",0);
        if(mf)ll_mouse_format=mf(NULL);if(kf)ll_key_format=kf(NULL);
    }
    ll_native_pop=resolve?(void*)resolve("UnityEngine.Event::PopEvent"):NULL;
    ll_ev_type=resolve?(void*)resolve("UnityEngine.Event::set_type"):NULL;
    ll_ev_key=resolve?(void*)resolve("UnityEngine.Event::set_keyCode"):NULL;
    ll_ev_mods=resolve?(void*)resolve("UnityEngine.Event::set_modifiers"):NULL;
    ll_ev_char=resolve?(void*)resolve("UnityEngine.Event::set_character"):NULL;
    if(guiImg && ll_native_pop && ll_ev_type && ll_ev_key && ll_ev_mods && ll_ev_char)ll_hook_method(cls,meth,guiImg,"UnityEngine","Event","PopEvent",1,(void*)ll_pop_event);
    else {logf("ERROR Unicode bridge incomplete");ll_native_pop=NULL;}

    void *(*method_return)(void*)=(void*)GetProcAddress(ga,"il2cpp_method_get_return_type");
    void *(*class_from_type)(void*)=(void*)GetProcAddress(ga,"il2cpp_class_from_type");
    void *(*field_named)(void*,const char*)=(void*)GetProcAddress(ga,"il2cpp_class_get_field_from_name");
    size_t (*field_offset)(void*)=(void*)GetProcAddress(ga,"il2cpp_field_get_offset");
    int (*value_size)(void*,uint32_t*)=(void*)GetProcAddress(ga,"il2cpp_class_value_size");
    ll_object_new=(void*)GetProcAddress(ga,"il2cpp_object_new");ll_array_new=(void*)GetProcAddress(ga,"il2cpp_array_new");
    ll_object_init=(void*)GetProcAddress(ga,"il2cpp_runtime_object_init");ll_write_barrier=(void*)GetProcAddress(ga,"il2cpp_gc_wbarrier_set_field");
    void *platform=cls(img,"","WindowsPlatformService");void *gm=platform?meth(platform,"GetAllMonitors",0):NULL;
    ll_monitor_value_class=cls(img,"","MonitorInfo");
    if(gm && method_return && class_from_type && field_named && field_offset && value_size && ll_object_new && ll_array_new && ll_object_init && ll_write_barrier) {
        ll_monitor_list_class=class_from_type(method_return(gm));
        void *fi=field_named(ll_monitor_list_class,"_items"),*fs=field_named(ll_monitor_list_class,"_size"),*fv=field_named(ll_monitor_list_class,"_version");
        uint32_t align=0;int size=ll_monitor_value_class?value_size(ll_monitor_value_class,&align):0;
        if(fi && fs && fv && size==sizeof(ll_monitor_value)) {
            ll_list_items_off=field_offset(fi);ll_list_size_off=field_offset(fs);ll_list_version_off=field_offset(fv);
            ll_hook_method(cls,meth,img,"","WindowsPlatformService","GetAllMonitors",0,(void*)ll_get_monitors);
            ll_hook_method(cls,meth,img,"","WindowsPlatformService","TryGetWorkArea",1,(void*)ll_workarea);
            logf("native monitor metadata valueSize=%d listOffsets=%d,%d,%d",size,ll_list_items_off,ll_list_size_off,ll_list_version_off);
        } else logf("ERROR native monitor metadata mismatch size=%d",size);
    } else logf("ERROR native monitor metadata API unavailable");

    ll_string_new=(void*)GetProcAddress(ga,"il2cpp_string_new");
    void *gw=platform?meth(platform,"GetDesktopObstacleWindows",1):NULL;
    ll_window_value_class=cls(img,"","DesktopWindowInfo");
    if(gw && ll_string_new && method_return && class_from_type && field_named && field_offset && value_size && ll_object_new && ll_array_new && ll_object_init && ll_write_barrier) {
        ll_window_list_class=class_from_type(method_return(gw));
        void *fi=field_named(ll_window_list_class,"_items"),*fs=field_named(ll_window_list_class,"_size"),*fv=field_named(ll_window_list_class,"_version");
        uint32_t align=0;int size=ll_window_value_class?value_size(ll_window_value_class,&align):0;
        if(fi && fs && fv && size==sizeof(ll_desktop_window) && field_offset(fi)==ll_list_items_off && field_offset(fs)==ll_list_size_off && field_offset(fv)==ll_list_version_off) {
            ll_hook_method(cls,meth,img,"","WindowsPlatformService","GetDesktopObstacleWindows",1,(void*)ll_get_windows);
            ll_hook_method(cls,meth,img,"","WindowsPlatformService","TryGetLiveWindowRect",2,(void*)ll_live_window);
            logf("KWin obstacle metadata verified: struct=%d cap=%d",size,LL_WINDOW_CAP);
        }else logf("ERROR KWin obstacle metadata mismatch size=%d",size);
    }else logf("ERROR KWin obstacle API unavailable");

    void *(*class_parent)(void*)=(void*)GetProcAddress(ga,"il2cpp_class_get_parent");
    ll_static_field_get=(void*)GetProcAddress(ga,"il2cpp_field_static_get_value");
    void *colliderClass=cls(img,"","ActiveWindowCollider");
    if(colliderClass && class_parent && field_named && ll_static_field_get) {
        void *parent=class_parent(colliderClass);
        ll_collider_instance_field=parent?field_named(parent,"s_instance"):NULL;
        void *active=field_named(colliderClass,"activeWindows"),*timer=field_named(colliderClass,"_windowUpdateTimer");
        if(active && timer){ll_active_windows_off=field_offset(active);ll_window_timer_off=field_offset(timer);}
        ll_collider_count_method=meth(colliderClass,"get_ColliderWindowCount",0);
        ll_collider_enabled_method=meth(colliderClass,"get_WindowCollidersEnabled",0);

    }

    ll_map_ready=(void*)ll_method(cls,meth,img,"","LilithWindow","get_IsReady",0);
    ll_desktop_world=(void*)ll_method(cls,meth,img,"","LilithWindow","DesktopToWorld",3);
    ll_world_desktop=(void*)ll_method(cls,meth,img,"","LilithWindow","WorldToDesktop",1);
    ll_get_cam=(void*)ll_method(cls,meth,img,"","ActiveWindowCollider","get_Cam",0);
    if(coreImg){
        ll_screen_world=(void*)ll_method(cls,meth,coreImg,"UnityEngine","Camera","ScreenToWorldPoint",1);
        ll_world_screen=(void*)ll_method(cls,meth,coreImg,"UnityEngine","Camera","WorldToScreenPoint",1);
    }
    if(ll_map_ready && ll_desktop_world && ll_world_desktop && ll_get_cam && ll_screen_world && ll_world_screen) {
        ll_hook_method(cls,meth,img,"","ActiveWindowCollider","Win32ToWorld",2,(void*)ll_collider_to_world);
        ll_hook_method(cls,meth,img,"","ActiveWindowCollider","TryWorldToWin32",3,(void*)ll_collider_to_desktop);
        ll_hook_method(cls,meth,img,"","ActiveWindowCollider","TryGetWindowRectByHandle",2,(void*)ll_live_window);
        logf("KWin unified mapping installed; cropped fallback / live bounds enabled");
    }else logf("ERROR KWin unified mapping prerequisites missing");

    char guard[16]={0};GetEnvironmentVariableA("LILITH_LINUX_WINDOW_MOTION_GUARD",guard,sizeof(guard));
    if(guard[0]=='0')ll_motion_enabled=0;
    ll_window_motion.speed=900;ll_window_motion.settle_ms=250;
    ll_runtime_method=meth;ll_object_class=(void*)GetProcAddress(ga,"il2cpp_object_get_class");
    void *characterClass=cls(img,"","CharacterController");
    if(characterClass && field_named && field_offset){
        ll_character_field=field_named(characterClass,"s_activeInstance");
        ll_hidden_method=meth(characterClass,"get_IsHiddenAway",0);
        void *rb=field_named(characterClass,"rb");if(rb)ll_character_rb_off=field_offset(rb);
    }
    char reboundEnv[16]={0};GetEnvironmentVariableA("LILITH_LINUX_WINDOW_REBOUND_GUARD",reboundEnv,sizeof(reboundEnv));
    ll_rebound_enabled=reboundEnv[0]!='0';
    if(characterClass && field_named && field_offset && class_parent) {
        ll_rebound_drag_method=meth(characterClass,"get_IsDrag",0);
        ll_rebound_bounds_method=meth(characterClass,"TryGetSpineWorldBounds",1);
        ll_rebound_height_method=meth(characterClass,"GetCharacterWorldHeight",0);
        void *timer=field_named(characterClass,"_cannotSettleTime"),*still=field_named(characterClass,"_settleStillTime");
        if(timer)ll_rebound_timer_off=field_offset(timer);if(still)ll_rebound_still_off=field_offset(still);
        void *hc=cls(img,"","CharacterInteractionHandler"),*hp=hc?class_parent(hc):NULL;
        ll_rebound_handler_field=hp?field_named(hp,"s_instance"):NULL;
        ll_rebound_clamp_method=hc?meth(hc,"ClampThrowUpVelocity",1):NULL;
    }
    if(!ll_character_rb_off || !ll_character_field || !ll_hidden_method || !ll_collider_instance_field || !ll_rebound_handler_field || !ll_rebound_clamp_method || !ll_rebound_drag_method || !ll_rebound_bounds_method || !ll_rebound_height_method || !ll_rebound_timer_off || !ll_object_class || !ll_runtime_method) {
        ll_rebound_enabled=0;logf("ERROR window rebound prerequisites missing");
    }
    logf("window rebound guard enabled=%d; original throw cap + contact-scoped watchdog rearm",ll_rebound_enabled);

    logf("KWin motion guard enabled=%d speed=%u px/s settle=%u ms",ll_motion_enabled,ll_window_motion.speed,ll_window_motion.settle_ms);
    logf("Unicode API pop=%p type=%p char=%p",ll_native_pop,ll_ev_type,ll_ev_char);
    logf("native input API queue=%p time=%p mouse=%p keys=%p id=%p",ll_queue_native,ll_native_time,ll_mouse_current,ll_key_current,ll_device_id);
    MemoryBarrier();ll_pump=ll_pump_input;
    char hotkeyEnv[16]={0};GetEnvironmentVariableA("LILITH_LINUX_HOTKEYS",hotkeyEnv,sizeof(hotkeyEnv));
    if(hotkeyEnv[0]!='0') {
        void *hc=cls(img,"","HotkeyService");
        void *comboClass=cls(img,"","HotkeyCombo");uint32_t align=0;
        void *cf=comboClass?field_named(comboClass,"Mods"):NULL,*kf=comboClass?field_named(comboClass,"Key"):NULL;
        ll_hotkey_combo=(void*)ll_method(cls,meth,img,"","HotkeyService","GetCombo",1);
        ll_hotkey_capturing=(void*)ll_method(cls,meth,img,"","HotkeyService","IsCapturing",1);
        ll_hotkey_invoke=(void*)ll_method(cls,meth,img,"","HotkeyService","Invoke",1);
        void *fire=hc?meth(hc,"TickFire",0):NULL,*key=hc?meth(hc,"IsKeyDown",1):NULL;
        void *mods=hc?meth(hc,"ReadHeldMods",1):NULL,*cancel=hc?meth(hc,"IsCancelKeyDown",0):NULL,*clear=hc?meth(hc,"IsClearKeyDown",0):NULL;
        // IL2CPP value-type field offsets include the 16-byte boxed object header.
        int comboSize=comboClass?value_size(comboClass,&align):0;
        if(comboSize==8 && cf && kf && field_offset(kf)-field_offset(cf)==4 &&
           ll_hotkey_combo && ll_hotkey_capturing && ll_hotkey_invoke && fire && key && mods && cancel && clear) {
            int keyOk=patch_jmp(*(void**)key,(void*)ll_hotkey_key_down);
            int captureOk=keyOk && patch_jmp(*(void**)mods,(void*)ll_hotkey_mods) &&
                patch_jmp(*(void**)cancel,(void*)ll_hotkey_cancel) && patch_jmp(*(void**)clear,(void*)ll_hotkey_clear);
            int fireOk=captureOk && patch_jmp(*(void**)fire,(void*)ll_hotkey_no_wine_fire);
            ll_hotkey_enabled=keyOk && fireOk;
            logf("native hotkeys enabled=%d comboSize=%d keyHook=%d fireHook=%d",ll_hotkey_enabled,comboSize,keyOk,fireOk);
        } else logf("ERROR native hotkey metadata validation failed size=%d",comboSize);
    }
    ll_install_desktop(ga,img,coreImg,cls,meth);
    ll_install_path_display(ga,img,coreImg,cls,meth);
    ll_install_tray(ga,img,cls,meth);
    ll_install_native(ga,img,coreImg,cls,meth);
    logf("input bridge ready frameCount=%p keyboard=%p",ll_frame_count,ll_keyboard_active);
}

static DWORD WINAPI worker(LPVOID arg) {
    HMODULE ga = NULL;
    for (int i = 0; i < 600 && !(ga = GetModuleHandleA("GameAssembly.dll")); i++) Sleep(50);
    if (!ga) { logf("GameAssembly.dll never loaded"); return 0; }
    logf("GameAssembly.dll at %p", ga);
    fn_v domain_get = (fn_v)GetProcAddress(ga, "il2cpp_domain_get");
    fn_p thread_attach = (fn_p)GetProcAddress(ga, "il2cpp_thread_attach");
    fn_asm get_asms = (fn_asm)GetProcAddress(ga, "il2cpp_domain_get_assemblies");
    fn_p asm_image = (fn_p)GetProcAddress(ga, "il2cpp_assembly_get_image");
    fn_name image_name = (fn_name)GetProcAddress(ga, "il2cpp_image_get_name");
    fn_cls class_from_name = (fn_cls)GetProcAddress(ga, "il2cpp_class_from_name");
    fn_meth get_method = (fn_meth)GetProcAddress(ga, "il2cpp_class_get_method_from_name");
    // il2cpp_init runs inside UnityPlayer before the player window appears; wait for our process' UnityWndClass
    HWND uw = NULL; DWORD me = GetCurrentProcessId();
    for (int i = 0; i < 1200 && !uw; i++) {
        Sleep(50);
        HWND w = NULL;
        while ((w = FindWindowExA(NULL, w, "UnityWndClass", NULL))) { DWORD pid = 0; GetWindowThreadProcessId(w, &pid); if (pid == me) { uw = w; break; } }
    }
    logf("unity window %p", uw);
    Sleep(1500);
    void *img = NULL, *inputImg = NULL, *systemImg=NULL, *guiImg=NULL, *coreImg=NULL; int attached = 0;
    for (int i = 0; i < 1200 && !img; i++) {
        void *dom = domain_get(); if (!dom) { Sleep(50); continue; }
        if (!attached) { logf("domain %p, attaching", dom); thread_attach(dom); attached = 1; logf("attached"); }
        size_t n = 0; void **a = get_asms(dom, &n);
        for (size_t k = 0; k < n; k++) {
            void *im = asm_image(a[k]);
            const char *nm = image_name(im); if (i == 0) logf("  image %s", nm);
            if(lstrcmpA(nm,"UnityEngine.CoreModule.dll")==0 || lstrcmpA(nm,"UnityEngine.CoreModule")==0)coreImg=im;
            if(lstrcmpA(nm,"UnityEngine.IMGUIModule.dll")==0 || lstrcmpA(nm,"UnityEngine.IMGUIModule")==0)guiImg=im;
            if (lstrcmpA(nm,"Unity.InputSystem.dll")==0 || lstrcmpA(nm,"Unity.InputSystem")==0) systemImg=im;
            if (lstrcmpA(nm, "UnityEngine.InputLegacyModule.dll") == 0 || lstrcmpA(nm, "UnityEngine.InputLegacyModule") == 0) inputImg=im;
            if (lstrcmpA(nm, "Assembly-CSharp.dll") == 0 || lstrcmpA(nm, "Assembly-CSharp") == 0) { img = im; }
        }
        if (!img) Sleep(50);
    }
    if (!img) { logf("Assembly-CSharp image not found"); return 0; }
    logf("Assembly-CSharp image %p", img);
    for (int i = 0; i < (int)(sizeof(probe)/sizeof(probe[0])); i++) {
        void *k = class_from_name(img, "", probe[i][0]);
        void *m = k ? get_method(k, probe[i][1], probe[i][2][0]-'0') : NULL;
        logf("%s.%s -> class %p method %p ptr %p", probe[i][0], probe[i][1], k, m, m ? *(void**)m : NULL);
    }
    install_input(ga,img,inputImg,systemImg,guiImg,coreImg,class_from_name,get_method);
    logf("probe done");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        char path[MAX_PATH]; GetModuleFileNameA(inst, path, MAX_PATH);
        char *s = path + lstrlenA(path); while (s > path && *s != '\\') s--; *s = 0;
        lstrcatA(path, "\\lilith_linux.log");
        g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        load_real();
        logf("lilith-linux proxy loaded");
        if (!GetEnvironmentVariableA("LILITH_LINUX_NOFRAME", NULL, 0)) { g_flip = GetEnvironmentVariableA("LILITH_LINUX_FLIP", NULL, 0) != 0; shm_open_(); install_frame_hooks(); g_suppress = !GetEnvironmentVariableA("LILITH_LINUX_PRESENT", NULL, 0); CreateThread(NULL, 0, present_hook_thread, NULL, 0, NULL); CreateThread(NULL, 0, stats_thread, NULL, 0, NULL); }
        CreateThread(NULL, 0, worker, NULL, 0, NULL);
    }
    return TRUE;
}
