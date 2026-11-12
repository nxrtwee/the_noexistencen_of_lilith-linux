// System UI is delegated to the current desktop's portal backend, not drawn by Lilith.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusArgument>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>
#include <QHash>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
extern "C" {
#include "../common/lilith_shm.h"
}
class DesktopBridge final:public QObject {
    Q_OBJECT
    struct Pending {ll_desktop_request *slot;uint32_t id;QString path;qint64 started;};
    ll_header *h;ll_desktop *d;QDBusConnection bus=QDBusConnection::sessionBus();
    QTimer poll;QElapsedTimer clock;QHash<uint32_t,Pending> jobs;
    static QString text(const uint16_t *src,int cap) {
        int n=0;while(n<cap && src[n])n++;
        if(n==cap)return {};return QString::fromUtf16(reinterpret_cast<const char16_t*>(src),n);
    }
    void disconnectRequest(const QString &path) {
        bus.disconnect("org.freedesktop.portal.Desktop",path,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
    }
    void cancel(uint32_t id) {
        if(!jobs.contains(id))return;auto p=jobs.take(id);
        disconnectRequest(p.path);
        bus.asyncCall(QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",p.path,"org.freedesktop.portal.Request","Close"));
        if(p.slot->id==id)__atomic_store_n(&p.slot->state,0,__ATOMIC_RELEASE);
        fprintf(stderr,"desktop: cancelled request id=%u\n",id);
    }
    void complete(uint32_t id,uint32_t status,QString result={}) {
        if(!jobs.contains(id))return;auto p=jobs.take(id);disconnectRequest(p.path);
        auto *s=p.slot;if(s->id!=id)return;
        uint32_t state=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE);
        if(state==5){__atomic_store_n(&s->state,0,__ATOMIC_RELEASE);return;}
        if(state!=3)return;
        if(result.size()>=LL_DESKTOP_PATH){status=2;result.clear();}
        s->status=status;const auto *utf=result.utf16();
        for(int i=0;i<result.size();i++)s->result[i]=utf[i];s->result[result.size()]=0;
        // Detached open calls are asynchronous requests, not a promise the external app has rendered.
        const uint32_t next=s->detached?0:4;
        uint32_t expected=3;
        if(!__atomic_compare_exchange_n(&s->state,&expected,next,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED) && expected==5)
            __atomic_store_n(&s->state,0,__ATOMIC_RELEASE);
        fprintf(stderr,"desktop: completed id=%u kind=%u status=%u\n",id,s->kind,status);
    }
    void start(ll_desktop_request *s) {
        const uint32_t id=s->id;
        QString sender=bus.baseService().mid(1);sender.replace('.','_');
        const QString token="lilith_desktop_"+QUuid::createUuid().toString(QUuid::Id128);
        const QString request="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
        jobs.insert(id,{s,id,request,clock.elapsed()});
        QVariantMap opts{{"handle_token",token}};QVariantList args;QString iface,method;
        if(s->kind==1) {
            iface="org.freedesktop.portal.FileChooser";method="OpenFile";
            opts.insert("directory",true);opts.insert("multiple",false);opts.insert("modal",true);
            QString title=text(s->title,LL_DESKTOP_TITLE);if(title.isEmpty())title=tr("Select a folder");
            args={QString(),title};
        } else {
            iface="org.freedesktop.portal.OpenURI";
            QString value=text(s->value,LL_DESKTOP_PATH);QUrl url;
            if(s->kind==3) {
                url=QUrl(value,QUrl::StrictMode);
                const QString scheme=url.scheme().toLower();
                if(!url.isValid() || !QStringList{"http","https","mailto","steam","file"}.contains(scheme)) {
                    fprintf(stderr,"desktop: rejected unsupported URI scheme id=%u\n",id);complete(id,2);return;
                }
                if(scheme=="file")value=url.toLocalFile();
                else {method="OpenURI";args={QString(),url.toString(QUrl::FullyEncoded)};}
            }
            if(method.isEmpty()) {
                QFileInfo info(value);
                // Local files/directories only. Never launch executables or special devices.
                if(!info.isAbsolute() || !info.exists() || (!info.isFile() && !info.isDir()) ||
                   (info.isFile() && (info.isExecutable() || info.suffix().compare("exe",Qt::CaseInsensitive)==0 || info.suffix().compare("desktop",Qt::CaseInsensitive)==0))) {
                    fprintf(stderr,"desktop: rejected invalid/non-document local target id=%u\n",id);complete(id,2);return;
                }
                int fd=::open(QFile::encodeName(info.absoluteFilePath()).constData(),O_RDONLY|O_CLOEXEC|O_NONBLOCK);
                if(fd<0){complete(id,2);return;}
                QDBusUnixFileDescriptor descriptor(fd);::close(fd);
                method=s->kind==4?"OpenDirectory":"OpenFile";
                args={QString(),QVariant::fromValue(descriptor)};
            }
            opts.insert("ask",false);
        }
        args.append(opts);
        bus.connect("org.freedesktop.portal.Desktop",request,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
        auto m=QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop",iface,method);m.setArguments(args);
        fprintf(stderr,"desktop: portal request id=%u kind=%u method=%s\n",id,s->kind,qPrintable(method));
        auto *watch=new QDBusPendingCallWatcher(bus.asyncCall(m),this);
        connect(watch,&QDBusPendingCallWatcher::finished,this,[this,watch,id] {
            QDBusPendingReply<QDBusObjectPath> reply=*watch;watch->deleteLater();
            if(!jobs.contains(id)) {
                if(!reply.isError())bus.asyncCall(QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",reply.value().path(),"org.freedesktop.portal.Request","Close"));
                return;
            }
            if(reply.isError()){fprintf(stderr,"desktop: portal call failed id=%u: %s\n",id,qPrintable(reply.error().message()));complete(id,2);return;}
            if(reply.value().path()!=jobs[id].path) {
                disconnectRequest(jobs[id].path);jobs[id].path=reply.value().path();
                bus.connect("org.freedesktop.portal.Desktop",jobs[id].path,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
            }
        });
    }
public:
    DesktopBridge(ll_header *header,QObject *parent):QObject(parent),h(header),d((ll_desktop*)((unsigned char*)header+LL_DESKTOP_OFF)) {
        if(qEnvironmentVariable("LILITH_LINUX_DESKTOP")=="0")return;
        if(!QCoreApplication::instance()->property("lilith.portal.identity").toBool()) {
            fprintf(stderr,"desktop: waiting for portal identity recovery\n");
        }
        auto *watcher=new QDBusServiceWatcher("org.freedesktop.portal.Desktop",bus,QDBusServiceWatcher::WatchForOwnerChange,this);
        connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString&,const QString&,const QString&){d->host_ready=0;const auto ids=jobs.keys();for(auto id:ids)complete(id,2);});
        clock.start();connect(&poll,&QTimer::timeout,this,[this]{tick();});poll.start(40);
    }
    ~DesktopBridge() override {d->host_ready=0;const auto ids=jobs.keys();for(auto id:ids)cancel(id);}
    void tick() {
        if(h->magic!=LL_MAGIC || h->version!=LL_VERSION || d->magic!=LL_DESKTOP_MAGIC)return;
        bool available=bus.isConnected() && QCoreApplication::instance()->property("lilith.portal.identity").toBool();
        __atomic_store_n(&d->host_ready,available?1:0,__ATOMIC_RELEASE);
        if(!available)return;
        const auto ids=jobs.keys();
        for(auto id:ids) {
            const auto &p=jobs[id];
            if(p.slot->state==5 || clock.elapsed()-p.started>300000) {
                if(p.slot->state==5)cancel(id);
                else {bus.asyncCall(QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",p.path,"org.freedesktop.portal.Request","Close"));complete(id,1);}
            }
        }
        for(auto &s:d->requests) {
            uint32_t state=__atomic_load_n(&s.state,__ATOMIC_ACQUIRE);
            if(state==5){__atomic_store_n(&s.state,0,__ATOMIC_RELEASE);continue;}
            uint32_t expected=2;
            if(state==2 && __atomic_compare_exchange_n(&s.state,&expected,3,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))start(&s);
        }
    }
public slots:
    void response(uint code,QVariantMap results,QDBusMessage message) {
        uint32_t id=0;for(auto it=jobs.cbegin();it!=jobs.cend();++it)if(it->path==message.path()){id=it.key();break;}
        if(!id)return;const auto p=jobs[id];
        if(code){complete(id,code==1?1:2);return;}
        if(p.slot->kind==1) {
            const QStringList uris=qdbus_cast<QStringList>(results.value("uris"));
            if(uris.size()!=1){complete(id,2);return;}
            const QUrl url(uris.first(),QUrl::StrictMode);const QString path=url.toLocalFile();
            QFileInfo info(path);
            if(!url.isLocalFile() || !info.isAbsolute() || !info.isDir()){complete(id,2);return;}
            complete(id,0,path);
        } else complete(id,0);
    }
};
QObject *ll_start_desktop(ll_header *header,QObject *parent){return new DesktopBridge(header,parent);}
#include "desktop_bridge.moc"
