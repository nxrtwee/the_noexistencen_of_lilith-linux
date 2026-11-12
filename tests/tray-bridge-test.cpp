// Run with QT_QPA_PLATFORM=offscreen dbus-run-session -- build/tray-bridge-test.
// Production tray renderer and IPC queue, with NO real panel or notification service.
#include "../src/host/tray_bridge.cpp"
#include <QDBusVirtualObject>
#include <QDBusArgument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <sys/mman.h>
#include <cassert>
class FakeNotifications:public QDBusVirtualObject {
public:
    int calls=0;
    QString introspect(const QString&)const override{return {};}
    bool handleMessage(const QDBusMessage &m,const QDBusConnection &bus)override {
        assert(m.member()=="Notify");const auto a=m.arguments();assert(a.size()==8);
        assert(a[0].toString()=="Lilith");assert(a[1].toUInt()==0);
        assert(a[3].toString()==QString::fromUtf8("Проверка"));assert(a[4].toString()=="&lt;b&gt;plain &amp; safe&lt;/b&gt;");
        assert(a[5].toStringList().isEmpty());auto hints=qdbus_cast<QVariantMap>(a[6]);
        assert(hints.value("desktop-entry").toString()=="org.lilith.LinuxBridge");assert(a[7].toInt()==5000);
        calls++;bus.send(m.createReply(QVariant::fromValue(uint(21))));return true;
    }
};
static void put(uint16_t *dst,int cap,const QString &s){assert(s.size()<cap);std::memset(dst,0,cap*2);std::memcpy(dst,s.utf16(),s.size()*2);}
static void events(int milliseconds=50){QElapsedTimer t;t.start();while(t.elapsed()<milliseconds)QCoreApplication::processEvents(QEventLoop::AllEvents,10);}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    auto bus=QDBusConnection::sessionBus();FakeNotifications fake;
    assert(bus.registerService("org.freedesktop.Notifications"));assert(bus.registerVirtualObject("/org/freedesktop/Notifications",&fake));
    void *memory=mmap(nullptr,LL_FILE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory!=MAP_FAILED);
    auto *header=(ll_header*)memory;auto *t=(ll_tray*)((char*)memory+LL_TRAY_OFF);t->magic=LL_TRAY_MAGIC;
    QTemporaryDir folder;QString icon=folder.path()+QString::fromUtf8("/значок.png");QImage image(32,32,QImage::Format_ARGB32);image.fill(Qt::red);assert(image.save(icon));
    t->snapshot.generation=1;t->snapshot.active=1;t->snapshot.count=5;
    put(t->snapshot.title,LL_TRAY_LABEL,QString::fromUtf8("Лилит"));put(t->snapshot.icon,LL_TRAY_PATH,icon);
    auto *items=t->snapshot.items;
    items[0].id=1;items[0].kind=0;items[0].enabled=1;put(items[0].label,LL_TRAY_LABEL,QString::fromUtf8("Настройки"));
    items[1].id=2;items[1].kind=1;items[1].enabled=1;put(items[1].label,LL_TRAY_LABEL,QString::fromUtf8("Музыка"));
    items[2].id=3;items[2].parent=2;items[2].enabled=1;put(items[2].label,LL_TRAY_LABEL,QString::fromUtf8("Пауза"));
    items[3].id=4;items[3].kind=2;
    items[4].id=5;put(items[4].label,LL_TRAY_LABEL,QString::fromUtf8("Недоступно"));t->seq=2;
    {
        LilithTray tray(header,nullptr,true);tray.poll();assert(t->host_status==1);assert(tray.menu.actions().size()==4);
        assert(tray.mainIcon.toolTip()==QString::fromUtf8("Лилит"));assert(tray.mainPath==icon);
        assert(tray.menu.actions()[1]->menu()->actions()[0]->text()==QString::fromUtf8("Пауза"));assert(tray.menu.actions()[2]->isSeparator());assert(!tray.menu.actions()[3]->isEnabled());
        tray.menu.actions()[0]->trigger();assert(t->event_write==1);assert(t->events[0].id==1 && t->events[0].generation==1);
        assert(!tray.enqueue(2,1));assert(!tray.enqueue(5,1));assert(!tray.enqueue(999,1));assert(!tray.enqueue(1,7));assert(!tray.enqueue(LL_TRAY_BADGE_ID,1));
        t->event_read=t->event_write;
        for(int i=0;i<LL_TRAY_EVENTS;i++)assert(tray.enqueue(3,1));assert(!tray.enqueue(3,1));assert(t->event_dropped==1);
        t->event_read=t->event_write=0xfffffff0u;
        for(int i=0;i<LL_TRAY_EVENTS;i++)assert(tray.enqueue(1,1));assert(!tray.enqueue(1,1));assert(t->event_write==16u);
        t->event_read=t->event_write;
        t->seq=3;t->snapshot.generation=2;tray.poll();assert(tray.snapshot->generation==1);
        t->seq=4;tray.poll();assert(tray.snapshot->generation==2);assert(!tray.enqueue(1,1));
        t->snapshot.badge_visible=1;put(t->snapshot.badge_tip,LL_TRAY_LABEL,QString::fromUtf8("Заметка"));t->seq=6;tray.poll();assert(tray.enqueue(LL_TRAY_BADGE_ID,2));
        auto &n=t->notifications[0];n.generation=2;n.duration=5000;put(n.title,LL_TRAY_LABEL,QString::fromUtf8("Проверка"));put(n.text,1024,"<b>plain & safe</b>");n.state=2;
        tray.poll();events();assert(n.state==0 && fake.calls==1);
        n.generation=1;n.state=2;tray.poll();events();assert(n.state==0 && fake.calls==1);
        t->snapshot.active=0;t->snapshot.count=0;t->seq=8;tray.poll();assert(!tray.enqueue(1,2));assert(tray.menu.actions().isEmpty());
    }
    assert(t->host_status==0);munmap(memory,LL_FILE_SIZE);
    puts("PASS: native Unicode menu/submenu/separator/disabled actions; icon file; generation isolation; odd-snapshot rejection; bounded command ring/wrap; badge event; native notification signature/plain-text escaping/stale rejection; disposal");
}
