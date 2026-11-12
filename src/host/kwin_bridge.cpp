// Temporary, read-only KWin script -> validated geometry snapshots.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVirtualObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>
#include <QSettings>
#include <QLocale>
void ll_native_context(QString,QString,bool,bool);
#include <QHash>
#include <QTimer>
#include <QVector>
#include <cstdio>
extern "C" {
#include "../common/lilith_shm.h"
}
static const char *PLUGIN="lilith-linux-windows";
class KWinBridge final : public QDBusVirtualObject {
    ll_header *header;
    QDBusConnection bus=QDBusConnection::sessionBus();
    QString owner;
    QHash<QString,uint64_t> ids;
    QVector<ll_window> windows;
    uint64_t nextId=0x4c4c00000000ULL;
    QTimer timer,health;
    bool started=false;
    int lastCount=-1;
public:
    explicit KWinBridge(ll_header *h,QObject *parent):QDBusVirtualObject(parent),header(h) {
        if(!bus.isConnected() || !bus.registerService("org.lilith.LinuxBridge") || !bus.registerVirtualObject("/LilithLinux",this)) {
            fprintf(stderr,"KWin: geometry receiver unavailable; window obstacles disabled\n");return;
        }
        startScript();
        QObject::connect(&timer,&QTimer::timeout,this,[this]{publish();});timer.start(33);
        QObject::connect(&health,&QTimer::timeout,this,[this]{
            QDBusReply<QString> current=bus.interface()->serviceOwner("org.kde.KWin");
            if(!current.isValid() || current.value()!=owner){windows.clear();publish();ll_native_context({}, {}, false,false);started=false;startScript();return;}
            QDBusInterface s("org.kde.KWin","/Scripting","org.kde.kwin.Scripting",bus);s.setTimeout(500);
            QDBusReply<bool> alive=s.call("isScriptLoaded",QString(PLUGIN));
            if(!alive.isValid() || !alive.value()){windows.clear();publish();started=false;startScript();}
        });health.start(2000);
    }
    bool startScript(){
        QDBusReply<QString> reply=bus.interface()->serviceOwner("org.kde.KWin");
        if(!reply.isValid()){fprintf(stderr,"KWin: no compositor service\n");return false;}owner=reply.value();
        QDBusInterface scripting("org.kde.KWin","/Scripting","org.kde.kwin.Scripting",bus);
        scripting.setTimeout(1000);
        scripting.call("unloadScript",QString(PLUGIN));
        QString file=QFileInfo(QCoreApplication::applicationDirPath()+"/../src/host/kwin-windows.js").canonicalFilePath();
        QDBusReply<int> loaded=scripting.call("loadScript",file,QString(PLUGIN));
        if(!loaded.isValid() || loaded.value()<0){fprintf(stderr,"KWin: load geometry script failed\n");return false;}
        QStringList paths={QString("/Scripting/Script%1").arg(loaded.value()),QString("/%1").arg(loaded.value())};
        for(const QString &path:paths) {
            QDBusInterface script("org.kde.KWin",path,"org.kde.kwin.Script",bus);
            if(!script.isValid())continue;
            QDBusMessage result=script.call("run");
            if(result.type()!=QDBusMessage::ErrorMessage){started=true;break;}
            fprintf(stderr,"KWin: script run failed: %s\n",qPrintable(result.errorMessage()));
        }
        if(!started){scripting.call("unloadScript",QString(PLUGIN));fprintf(stderr,"KWin: cannot run geometry script\n");return false;}
        fprintf(stderr,"KWin: read-only geometry feed started (no window titles)\n");
        return true;
    }
    void recover(){windows.clear();publish();ll_native_context({}, {}, false,false);started=false;startScript();}
    ~KWinBridge() override {
        timer.stop();health.stop();windows.clear();publish();
        if(started){QDBusInterface s("org.kde.KWin","/Scripting","org.kde.kwin.Scripting",bus);s.call("unloadScript",QString(PLUGIN));}
        bus.unregisterObject("/LilithLinux");bus.unregisterService("org.lilith.LinuxBridge");
    }
    QString introspect(const QString &) const override {
        return QStringLiteral("<interface name=\"org.lilith.LinuxBridge\"><method name=\"publish\"><arg name=\"geometry\" type=\"s\" direction=\"in\"/></method><method name=\"context\"><arg name=\"context\" type=\"s\" direction=\"in\"/></method></interface>");
    }
    bool handleMessage(const QDBusMessage &message,const QDBusConnection &connection) override {
        if(message.interface()!="org.lilith.LinuxBridge" || (message.member()!="publish" && message.member()!="context"))return false;
        if(message.service()!=owner || message.arguments().size()!=1) {
            connection.send(message.createErrorReply("org.freedesktop.DBus.Error.AccessDenied","KWin sender required"));return true;
        }
        QByteArray bytes=message.arguments()[0].toString().toUtf8();
        QJsonParseError error;QJsonDocument document=QJsonDocument::fromJson(bytes,&error);
        if(message.member()=="context"){
            if(bytes.size()>4096 || error.error!=QJsonParseError::NoError || !document.isObject()){connection.send(message.createErrorReply("org.freedesktop.DBus.Error.InvalidArgs","Invalid desktop context"));return true;}
            QJsonObject v=document.object();int pid=v["pid"].toInt();QString name=v["cls"].toString().left(255),desktop=v["desktop"].toString().left(255),label=name;
            if(pid>0){QFile f(QString("/proc/%1/comm").arg(pid));if(f.open(QIODevice::ReadOnly)){QString comm=QString::fromUtf8(f.read(256)).trimmed();if(!comm.isEmpty())name=comm;}}
            if(!desktop.isEmpty() && !desktop.contains('/') && !desktop.contains("..")){
                QString file=QStandardPaths::locate(QStandardPaths::GenericDataLocation,"applications/"+desktop+(desktop.endsWith(".desktop")?"":".desktop"));
                if(!file.isEmpty()){QSettings settings(file,QSettings::IniFormat);QString key="Desktop Entry/Name["+QLocale().name()+"]";label=settings.value(key,settings.value("Desktop Entry/Name",label)).toString().left(255);}
            }
            ll_native_context(name,label,v["fullscreen"].toBool(),pid>0 || !name.isEmpty());connection.send(message.createReply());return true;
        }
        if(bytes.size()>32768 || error.error!=QJsonParseError::NoError || !document.isArray()) {
            connection.send(message.createErrorReply("org.freedesktop.DBus.Error.InvalidArgs","Invalid geometry snapshot"));return true;
        }
        QVector<ll_window> next;
        for(const QJsonValue &value:document.array()) {
            QJsonObject v=value.toObject();QString id=v["id"].toString();
            int x=v["x"].toInt(),y=v["y"].toInt(),w=v["w"].toInt(),h=v["h"].toInt();
            if(id.isEmpty() || id.size()>80 || w<32 || h<24 || w>32768 || h>32768 || x<-65536 || x>65536 || y<-65536 || y>65536)continue;
            if(!ids.contains(id))ids.insert(id,++nextId);
            next.push_back({ids.value(id),x,y,w,h,(uint32_t)(v["flags"].toInt()&1),0});
            if(next.size()==LL_WINDOW_CAP)break;
        }
        windows=next;
        if(lastCount!=windows.size()){lastCount=windows.size();fprintf(stderr,"KWin: %d visible window obstacle(s)\n",lastCount);}
        connection.send(message.createReply());return true;
    }
    void publish() {
        uint32_t base=__atomic_load_n(&header->windows_seq,__ATOMIC_RELAXED)&~1u;
        __atomic_store_n(&header->windows_seq,base+1,__ATOMIC_RELEASE);
        for(int i=0;i<windows.size();i++)header->windows[i]=windows[i];
        header->window_count=windows.size();
        __atomic_store_n(&header->windows_seq,base+2,__ATOMIC_RELEASE);
    }
};
QObject *ll_start_kwin(ll_header *header,QObject *parent){return new KWinBridge(header,parent);}

void ll_recover_kwin(QObject *object){if(auto *bridge=dynamic_cast<KWinBridge*>(object))bridge->recover();}
