// Makes the Wine X11 window of the layered hwnd invisible and click-through (XWayland / X11 sessions).
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <cstdio>
#include <cstdint>

static Display *g_dpy = nullptr;
static int x_err(Display *, XErrorEvent *e) { fprintf(stderr, "lilith-host: X error %d (req %d)\n", e->error_code, e->request_code); return 0; }

bool ll_x11_neutralize(uint64_t win) {
    if (!win) return false;
    if (!g_dpy) { g_dpy = XOpenDisplay(nullptr); if (!g_dpy) return false; XSetErrorHandler(x_err); }
    Window w = (Window)win;
    Atom op = XInternAtom(g_dpy, "_NET_WM_WINDOW_OPACITY", False);
    unsigned long zero = 0;
    XChangeProperty(g_dpy, w, op, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&zero, 1);
    XShapeCombineRectangles(g_dpy, w, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    // also hide it from taskbar / pager / alt-tab
    Atom st = XInternAtom(g_dpy, "_NET_WM_STATE", False);
    Atom v[2] = { XInternAtom(g_dpy, "_NET_WM_STATE_SKIP_TASKBAR", False), XInternAtom(g_dpy, "_NET_WM_STATE_SKIP_PAGER", False) };
    XEvent ev = {}; ev.xclient.type = ClientMessage; ev.xclient.window = w; ev.xclient.message_type = st; ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1; ev.xclient.data.l[1] = v[0]; ev.xclient.data.l[2] = v[1]; ev.xclient.data.l[3] = 1;
    XSendEvent(g_dpy, DefaultRootWindow(g_dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(g_dpy);
    return true;
}
