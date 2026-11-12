// dbus-run-session -- build/desktop-bridge-test (never touches the real desktop bus)
#include "../src/host/desktop_bridge.cpp"
#include <QDBusVirtualObject>
#include <QTemporaryDir>
#include <QDir>
#include <QEventLoop>
#include <sys/mman.h>
#include <cassert>
class FakeDesktopPortal:public QDBusVirtualObject {
public:
    QDBusConnection bus=QDBusConnection::sessionBus();QString chosen;uint code=0;int calls=0,closes=0,delay=0;
    QString introspect(const QString &)const override{return {};}
    bool handleMessage(const QDBusMessage &m,const QDBusConnection &b)override {
        if(m.member()=="Close"){closes++;b.send(m.createReply());return true;}
        const auto a=m.arguments();assert(a.size()==3);calls++;
        const auto opts=qdbus_cast<QVariantMap>(a[2]);
        if(m.interface()=="org.freedesktop.portal.FileChooser") {
            assert(m.member()=="OpenFile");assert(opts.value("directory").toBool());assert(!opts.value("multiple").toBool());
            assert(a[1].toString()==QString::fromUtf8("Lilith: выбор папки"));
        } else {
            assert(m.interface()=="org.freedesktop.portal.OpenURI");
            if(m.member()=="OpenFile") {
                auto fd=qdbus_cast<QDBusUnixFileDescriptor>(a[1]);assert(fd.isValid());
            } else {assert(m.member()=="OpenURI");assert(a[1].toString().startsWith("https://"));}
        }
        QString sender=m.service().mid(1);sender.replace('.','_');
        QString path="/org/freedesktop/portal/desktop/request/"+sender+"/"+opts.value("handle_token").toString();
        b.send(m.createReply({QVariant::fromValue(QDBusObjectPath(path))}));
        QVariantMap results;
        if(m.interface()=="org.freedesktop.portal.FileChooser")results.insert("uris",QStringList{chosen});
        const uint responseCode=code;
        QTimer::singleShot(delay,this,[this,path,results,responseCode]{
            auto sig=QDBusMessage::createSignal(path,"org.freedesktop.portal.Request","Response");
            sig.setArguments({responseCode,results});bus.send(sig);
        });return true;
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setProperty("lilith.portal.identity",true);
    void *mapping=mmap(nullptr,LL_FILE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(mapping!=MAP_FAILED);
    auto *h=(ll_header*)mapping;h->magic=LL_MAGIC;h->version=LL_VERSION;
    auto *d=(ll_desktop*)((char*)h+LL_DESKTOP_OFF);d->magic=LL_DESKTOP_MAGIC;
    QTemporaryDir temp;assert(temp.isValid());
    QString folder=temp.path()+QString::fromUtf8("/Папка с пробелами");assert(QDir().mkpath(folder));
    FakeDesktopPortal portal;portal.chosen=QUrl::fromLocalFile(folder).toString(QUrl::FullyEncoded);
    assert(portal.bus.registerService("org.freedesktop.portal.Desktop"));
    assert(portal.bus.registerVirtualObject("/org/freedesktop/portal/desktop",&portal,QDBusConnection::SubPath));
    auto wait=[&](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    auto put=[](uint16_t *dst,QString value){for(int i=0;i<value.size();i++)dst[i]=value.utf16()[i];dst[value.size()]=0;};
    uint32_t serial=0;
    auto request=[&](uint32_t kind,QString value={}) {
        auto *s=&d->requests[0];assert(s->state==0);s->id=++serial;s->kind=kind;s->detached=0;s->status=99;
        put(s->title,QString::fromUtf8("Lilith: выбор папки"));put(s->value,value);s->result[0]=0;
        __atomic_store_n(&s->state,2,__ATOMIC_RELEASE);return s;
    };
    {
        DesktopBridge bridge(h,&app);wait(80);assert(d->host_ready==1);
        auto *s=request(1);wait(180);assert(s->state==4 && s->status==0);
        assert(QString::fromUtf16((char16_t*)s->result)==folder);s->state=0;
        portal.code=1;s=request(1);wait(180);assert(s->state==4 && s->status==1 && s->result[0]==0);s->state=0;
        portal.code=0;portal.chosen="https://not-a-local-folder.invalid/";s=request(1);wait(180);assert(s->status==2);s->state=0;
        int before=portal.calls;s=request(3,"javascript:alert(1)");wait(100);assert(s->status==2 && portal.calls==before);s->state=0;
        s=request(2,"/a/path/that/does/not/exist");wait(100);assert(s->status==2 && portal.calls==before);s->state=0;
        s=request(2,folder);wait(180);assert(s->status==0 && s->state==4);s->state=0;
        s=request(3,"https://example.invalid/document?q=a%20b");wait(180);assert(s->status==0);s->state=0;
        portal.chosen=QUrl::fromLocalFile(folder).toString();portal.delay=250;
        s=request(1);wait(80);assert(s->state==3);s->state=5;wait(100);assert(s->state==0);wait(250);assert(s->state==0 && portal.closes>0);
        s=request(2,folder);s->detached=1;wait(400);assert(s->state==0);
        assert(sizeof(ll_header)<=LL_BUF_OFF);
    }
    assert(d->host_ready==0);munmap(mapping,LL_FILE_SIZE);
    puts("PASS: chooser portal signature; Unicode/spaces roundtrip; cancel/error; non-local rejection; unsafe URI/missing-path rejection; FD local open; URI open; cancellation/late reply; detached cleanup; shutdown");
}
