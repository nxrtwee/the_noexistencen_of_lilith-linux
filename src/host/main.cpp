// lilith-host: native Linux presenter for The NOexistenceN of Lilith (stage 2: frames only)
#include <QApplication>
#include <QRasterWindow>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QScreen>
#include <QImage>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QInputMethodEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QRegion>
#include <QLockFile>
#include <QStandardPaths>
#include <vector>
#include <algorithm>
#include <LayerShellQt/Window>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
extern "C" {
#include "../common/lilith_shm.h"
}

bool ll_x11_neutralize(uint64_t win);
QObject *ll_start_kwin(ll_header *header,QObject *parent);
QObject *ll_start_hotkeys(ll_header *header,QObject *parent);
QObject *ll_start_desktop(ll_header *header,QObject *parent);
QObject *ll_start_tray(ll_header *header,QObject *parent);
QObject *ll_start_native(ll_header *header,QObject *parent);
bool ll_native_paste();bool ll_native_paste_focus(ll_header *header);bool ll_native_locked(ll_header *header);
bool ll_native_drop(const QMimeData *mime,QPoint desktop,bool copy);
void ll_recover_kwin(QObject*);void ll_recover_hotkeys(QObject*);
static QObject *nativeKwin=nullptr,*nativeHotkeys=nullptr;
void ll_native_recovery(){if(nativeKwin)ll_recover_kwin(nativeKwin);if(nativeHotkeys)ll_recover_hotkeys(nativeHotkeys);}
static ll_header *g_hdr = nullptr;
static unsigned char *g_bufs[LL_BUFFERS];

static bool open_shm() {
    int fd = open(LL_SHM_UNIX, O_RDWR | O_CREAT, 0600);
    if (fd < 0) { perror("open shm"); return false; }
    if (ftruncate(fd, LL_FILE_SIZE) != 0) { perror("ftruncate");close(fd);return false; }
    void *p = mmap(nullptr, LL_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { perror("mmap"); return false; }
    g_hdr = (ll_header *)p;
    for(int i=0;i<LL_BUFFERS;i++)g_bufs[i]=(unsigned char*)p+LL_BUF_OFF+i*LL_BUF_SIZE;
    for(int i=0;i<LL_BUFFERS;i++)if(g_hdr->buffer_state[i]==2)__atomic_store_n(&g_hdr->buffer_state[i],0,__ATOMIC_RELEASE);
    g_hdr->host_alive = 1;
    return true;
}

class Overlay : public QRasterWindow {
public:
    explicit Overlay(QScreen *s) : scr(s) {
        setScreen(s);
        setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        QSurfaceFormat f = format(); f.setAlphaBufferSize(8); setFormat(f);
        if (auto lw = LayerShellQt::Window::get(this)) {
            lw->setLayer(LayerShellQt::Window::LayerOverlay);
            lw->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
                                                          LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
            lw->setExclusiveZone(-1);
            lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            lw->setScope(QStringLiteral("lilith-linux"));
            lw->setScreen(s);
        }
        setGeometry(s->geometry());
        connect(s,&QScreen::geometryChanged,this,[this,s](const QRect &g){setGeometry(g);updateInputMask();update();});
        connect(s,&QScreen::logicalDotsPerInchChanged,this,[this]{updateInputMask();update();});
        setMask(QRegion(-10,-10,1,1));
    }
    void setFrame(const QImage *img, QPoint desktopPos, const QRect &prevDesktop) {
        frame = img; pos = desktopPos;
        QRect now(pos, img ? img->size() : QSize());
        QRect dirty = (now | prevDesktop).translated(-scr->geometry().topLeft()) & QRect(QPoint(), size());
        if (!dirty.isEmpty()) update(dirty);
        // Wayland QWindow masks are input regions, not visual clipping.
        if (maskClock.elapsed() > 33 || !maskClock.isValid()) { updateInputMask(); maskClock.start(); }
    }
    void syncKeyboard() {
        bool capturing=g_hdr->hotkey_capturing!=0;
        bool enabled = !ll_native_locked(g_hdr) && ((g_hdr->game_flags & LL_KEYBOARD) != 0 || capturing || ll_native_paste_focus(g_hdr));
        if(enabled == keyboard) return;
        keyboard=enabled;
        if(auto *lw=LayerShellQt::Window::get(this))
            lw->setKeyboardInteractivity(enabled ? LayerShellQt::Window::KeyboardInteractivityOnDemand : LayerShellQt::Window::KeyboardInteractivityNone);
        if(enabled && capturing)requestActivate();
        fprintf(stderr,"input: keyboard %s (click field to focus)\n", enabled ? "on" : "off");
    }
    void updateInputMask() {
        QRegion region;
        if(frame && !(g_hdr->game_flags & LL_PASSTHROUGH)) {
            const int tile=8;
            for(int y=0;y<frame->height();y+=tile) {
                int run=-1;
                for(int x=0;x<=frame->width();x+=tile) {
                    bool solid=false;
                    if(x<frame->width()) {
                        for(int yy=y;yy<std::min(y+tile,frame->height()) && !solid;++yy) {
                            auto row=(const QRgb*)frame->constScanLine(yy);
                            for(int xx=x;xx<std::min(x+tile,frame->width());++xx) if(qAlpha(row[xx])>16) {solid=true;break;}
                        }
                    }
                    if(solid && run<0) run=x;
                    if(!solid && run>=0) {
                        region += QRect(run,y,x-run,std::min(tile,frame->height()-y)); run=-1;
                    }
                }
                if(run>=0) region += QRect(run,y,frame->width()-run,std::min(tile,frame->height()-y));
            }
            region.translate(pos-scr->geometry().topLeft());
            region &= QRect(QPoint(),size());
        }
        if(region.isEmpty()) region=QRegion(-10,-10,1,1); // empty Qt mask means full input!
        if(region!=inputMask) {setMask(region); inputMask=region;}
    }
    bool ownsScreen(const QScreen *screen) const{return scr==screen;}
protected:
    void point(const QPointF &local) {
        QPoint desktop=local.toPoint()+scr->geometry().topLeft();
        // Current protocol uses desktop pixels; initial single-screen 1x path.
        g_hdr->mouse_x=desktop.x(); g_hdr->mouse_y=desktop.y(); g_hdr->pointer_inside=1;
    }
    static int buttonIndex(Qt::MouseButton b) {return b==Qt::LeftButton?0:b==Qt::RightButton?1:b==Qt::MiddleButton?2:-1;}
    void mouseMoveEvent(QMouseEvent *e) override {point(e->position());}
    void mousePressEvent(QMouseEvent *e) override {
        point(e->position()); int b=buttonIndex(e->button());
        if(b>=0) {g_hdr->mouse_buttons |= 1u<<b; ++g_hdr->mouse_down[b];}
        syncKeyboard(); if(keyboard) requestActivate();
        fprintf(stderr,"input: mouse down %d at %d,%d\n",b,g_hdr->mouse_x,g_hdr->mouse_y);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        point(e->position()); int b=buttonIndex(e->button());
        if(b>=0) {g_hdr->mouse_buttons &= ~(1u<<b); ++g_hdr->mouse_up[b];}
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {mousePressEvent(e);}
    void wheelEvent(QWheelEvent *e) override {point(e->position()); enqueue(4,(uint32_t)e->angleDelta().y());}
    void pointerLeave() {g_hdr->pointer_inside=0; if(!g_hdr->mouse_buttons) {g_hdr->mouse_x=-10000;g_hdr->mouse_y=-10000;}}
    static void enqueue(uint32_t type,uint32_t code,uint32_t mods=0) {
        uint32_t wr=__atomic_load_n(&g_hdr->event_write,__ATOMIC_RELAXED);
        uint32_t rd=__atomic_load_n(&g_hdr->event_read,__ATOMIC_ACQUIRE);
        if(wr-rd>=LL_EVENT_CAP) {++g_hdr->event_dropped;++g_hdr->input_reset;return;}
        g_hdr->events[wr%LL_EVENT_CAP]={type,code,mods,0};
        __atomic_store_n(&g_hdr->event_write,wr+1,__ATOMIC_RELEASE);
    }
    static int virtualKey(int k) {
        if(k>=Qt::Key_A && k<=Qt::Key_Z) return k;
        if(k>=Qt::Key_0 && k<=Qt::Key_9) return k;
        if(k>=Qt::Key_F1 && k<=Qt::Key_F24) return 0x70+k-Qt::Key_F1;
        switch(k) {
        case Qt::Key_Backspace:return 8; case Qt::Key_Tab:return 9;
        case Qt::Key_Return:case Qt::Key_Enter:return 13;
        case Qt::Key_Meta:return 91; case Qt::Key_Shift:return 16; case Qt::Key_Control:return 17; case Qt::Key_Alt:return 18;
        case Qt::Key_Escape:return 27; case Qt::Key_Space:return 32;
        case Qt::Key_PageUp:return 33; case Qt::Key_PageDown:return 34;
        case Qt::Key_End:return 35; case Qt::Key_Home:return 36;
        case Qt::Key_Left:return 37; case Qt::Key_Up:return 38;case Qt::Key_Right:return 39;case Qt::Key_Down:return 40;
        case Qt::Key_Insert:return 45;case Qt::Key_Delete:return 46;
        default:return 0;
        }
    }
    static uint32_t modifiers(Qt::KeyboardModifiers m) {
        return ((m&Qt::ShiftModifier)?1u:0u)|((m&Qt::ControlModifier)?2u:0u)|((m&Qt::AltModifier)?4u:0u)|((m&Qt::MetaModifier)?8u:0u);
    }
    static int shortcutVk(QKeyEvent *e) {
        // Qt Wayland nativeScanCode is the XKB keycode (evdev + 8).
        if(QGuiApplication::platformName()=="wayland") {
            const uint32_t scan=e->nativeScanCode();
            static const char top[]="QWERTYUIOP",home[]="ASDFGHJKL",bottom[]="ZXCVBNM";
            if(scan>=24 && scan<=33)return top[scan-24];
            if(scan>=38 && scan<=46)return home[scan-38];
            if(scan>=52 && scan<=58)return bottom[scan-52];
            switch(scan) {
            case 34:return 219;case 35:return 221;case 47:return 186;case 48:return 222;
            case 49:return 192;case 51:return 220;case 59:return 188;case 60:return 190;
            case 61:return 191;case 20:return 189;case 21:return 187;
            }
        }
        return virtualKey(e->key());
    }
    static void focusedKey(int vk,bool down,Qt::KeyboardModifiers mods) {
        auto set=[](int key,bool on) {
            if(key<=0 || key>=256)return;
            uint32_t bit=1u<<(key%32);
            if(on)__atomic_fetch_or(&g_hdr->focused_vk[key/32],bit,__ATOMIC_RELEASE);
            else __atomic_fetch_and(&g_hdr->focused_vk[key/32],~bit,__ATOMIC_RELEASE);
        };
        set(vk,down);
        set(16,mods&Qt::ShiftModifier);set(17,mods&Qt::ControlModifier);
        set(18,mods&Qt::AltModifier);set(91,mods&Qt::MetaModifier);
        // The released modifier must not stay held even on platforms reporting old modifiers.
        if(!down && (vk==16 || vk==17 || vk==18 || vk==91))set(vk,false);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if(!e->isAutoRepeat() && e->key()==Qt::Key_V && e->modifiers()==Qt::ControlModifier && ll_native_paste()){e->accept();return;}
        uint32_t mods=modifiers(e->modifiers()); int vk=virtualKey(e->key());focusedKey(shortcutVk(e),true,e->modifiers());if(vk)enqueue(1,vk,mods);
        if(!(e->modifiers() & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)))
            for(QChar c:e->text()) if(c.unicode()>=32 && c.unicode()!=127)enqueue(3,c.unicode(),mods);
        e->accept();
    }
    void keyReleaseEvent(QKeyEvent *e) override {
        if(!e->isAutoRepeat()) {int vk=virtualKey(e->key());focusedKey(shortcutVk(e),false,e->modifiers());if(vk)enqueue(2,vk,modifiers(e->modifiers()));} e->accept();
    }
    bool event(QEvent *e) override {
        if(e->type()==QEvent::DragEnter || e->type()==QEvent::DragMove){
            auto *d=static_cast<QDragMoveEvent*>(e);bool local=d->mimeData()->hasUrls();
            for(const auto &url:d->mimeData()->urls())local=local && url.isLocalFile();
            if(local && !ll_native_locked(g_hdr) && !(g_hdr->game_flags&LL_PASSTHROUGH)){d->acceptProposedAction();return true;}
            d->ignore();return true;
        }
        if(e->type()==QEvent::Drop){auto *d=static_cast<QDropEvent*>(e);QPoint desktop=d->position().toPoint()+scr->geometry().topLeft();
            if(ll_native_drop(d->mimeData(),desktop,d->proposedAction()!=Qt::MoveAction)){d->acceptProposedAction();return true;}d->ignore();return true;}

        if(e->type()==QEvent::Leave) pointerLeave();
        if(e->type()==QEvent::FocusOut) {
            for(auto &word:g_hdr->focused_vk)__atomic_store_n(&word,0,__ATOMIC_RELEASE);
            uint32_t buttons=g_hdr->mouse_buttons;g_hdr->mouse_buttons=0;
            for(int i=0;i<3;i++)if(buttons&(1u<<i))++g_hdr->mouse_up[i];
            ++g_hdr->input_reset;
        }
        if(e->type()==QEvent::InputMethod) {
            auto *im=static_cast<QInputMethodEvent*>(e);
            for(QChar c:im->commitString()) enqueue(3,c.unicode());
            e->accept();return true;
        }
        return QRasterWindow::event(e);
    }
    void paintEvent(QPaintEvent *e) override {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(e->rect(), Qt::transparent);
        if (frame) {
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setClipRect(e->rect());
            p.drawImage(pos - scr->geometry().topLeft(), *frame);
        }
    }
private:
    QScreen *scr; const QImage *frame = nullptr; QPoint pos;
    QRegion inputMask; QElapsedTimer maskClock; bool keyboard=false;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("Lilith");
    app.setApplicationDisplayName("Lilith");
    app.setDesktopFileName("org.lilith.LinuxBridge");
    QLockFile lock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/lilith-host.lock");
    if(!lock.tryLock(0)){fprintf(stderr,"lilith-host: another presenter is running\n");return 2;}
    if (!open_shm()) return 1;
    QObject *kwin=ll_start_kwin(g_hdr,&app);
    QObject *hotkeys=ll_start_hotkeys(g_hdr,&app);
    QObject *desktop=ll_start_desktop(g_hdr,&app);
    QObject *tray=ll_start_tray(g_hdr,&app);
    QObject *native=ll_start_native(g_hdr,&app);
    nativeKwin=kwin;nativeHotkeys=hotkeys;
    std::vector<Overlay *> wins;
    for (QScreen *s : app.screens()) { auto *w = new Overlay(s); w->show(); wins.push_back(w); }
    QObject::connect(&app,&QGuiApplication::screenAdded,&app,[&](QScreen *s){auto *w=new Overlay(s);w->show();wins.push_back(w);++g_hdr->input_reset;});
    QObject::connect(&app,&QGuiApplication::screenRemoved,&app,[&](QScreen *s){for(auto it=wins.begin();it!=wins.end();){if((*it)->ownsScreen(s)){delete *it;it=wins.erase(it);}else ++it;}++g_hdr->input_reset;});
    fprintf(stderr, "lilith-host: %zu screen(s), platform=%s\n", wins.size(), qPrintable(app.platformName()));

    for(auto *sc:app.screens()) {QRect g=sc->geometry(),a=sc->availableGeometry();fprintf(stderr,"native screen %dx%d work=%d,%d %dx%d\n",g.width(),g.height(),a.x(),a.y(),a.width(),a.height());}
    QImage img; QRect lastRect; uint32_t lastSeq = 0; int held=-1; long frames = 0; QElapsedTimer stat; stat.start();
    QTimer t; t.setTimerType(Qt::PreciseTimer);
    QObject::connect(&t, &QTimer::timeout, [&] {
        g_hdr->host_alive=1; ++g_hdr->host_heartbeat;
        auto screens=app.screens();uint32_t count=std::min(8,(int)screens.size());
        for(uint32_t i=0;i<count;i++) {
            QRect g=screens[i]->geometry(),a=screens[i]->availableGeometry();
            int32_t rect[8]={g.x(),g.y(),g.width(),g.height(),a.x(),a.y(),a.width(),a.height()};
            std::copy(rect,rect+8,g_hdr->screen_rects[i]);
        }
        __atomic_store_n(&g_hdr->screen_count,count,__ATOMIC_RELEASE);
        for(auto *win:wins){win->setVisible(!ll_native_locked(g_hdr));win->syncKeyboard();}
        uint32_t seq = __atomic_load_n(&g_hdr->seq,__ATOMIC_ACQUIRE);
        if (seq != lastSeq && g_hdr->magic == LL_MAGIC && g_hdr->version == LL_VERSION) {
            uint32_t idx=__atomic_load_n(&g_hdr->cur,__ATOMIC_ACQUIRE)%LL_BUFFERS;
            uint32_t expected=0;
            if(!__atomic_compare_exchange_n(&g_hdr->buffer_state[idx],&expected,2,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
            int w = g_hdr->frame_w[idx], h = g_hdr->frame_h[idx];
            QPoint pos(g_hdr->frame_x[idx], g_hdr->frame_y[idx]);
            if (w > 0 && h > 0 && w <= LL_MAX_W && h <= LL_MAX_H) {
                int old=held;held=idx;
                img = QImage(g_bufs[idx], w, h, w * 4, QImage::Format_ARGB32_Premultiplied);
                if(old>=0)__atomic_store_n(&g_hdr->buffer_state[old],0,__ATOMIC_RELEASE);
                lastSeq=seq;
                QRect r(pos, img.size());
                for (auto *win : wins) win->setFrame(&img, pos, lastRect);
                lastRect = r; frames++;
            } else __atomic_store_n(&g_hdr->buffer_state[idx],0,__ATOMIC_RELEASE);
        }
        if (stat.elapsed() > 10000) { fprintf(stderr, "lilith-host: %.1f fps, rect=%d,%d %dx%d\n", frames * 1000.0 / stat.elapsed(), lastRect.x(), lastRect.y(), lastRect.width(), lastRect.height()); frames = 0; stat.restart(); }
    });
    t.start(8);
    // keep the Wine X11 window transparent + click-through (re-applied: Wine may reset shape/props)
    uint64_t lastX = 0; QTimer xt;
    QObject::connect(&xt, &QTimer::timeout, [&] {
        uint64_t xw = g_hdr->x11_window;
        if (xw && ll_x11_neutralize(xw) && xw != lastX) { fprintf(stderr, "lilith-host: neutralized X11 window 0x%llx\n", (unsigned long long)xw); lastX = xw; }
    });
    xt.start(1000);
    int result=app.exec();
    nativeKwin=nullptr;nativeHotkeys=nullptr;
    delete native;
    delete tray;
    delete desktop;
    delete hotkeys;
    delete kwin;
    if(held>=0)__atomic_store_n(&g_hdr->buffer_state[held],0,__ATOMIC_RELEASE);
    g_hdr->host_alive=0;g_hdr->mouse_buttons=0;
    return result;
}
