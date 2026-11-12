// Global shortcuts via XDG Desktop Portal. No keyboard grabs or global key polling.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QTimer>
#include <QSet>
#include <QUuid>
#include <QVariantMap>
#include <cstdio>
extern "C" {
#include "../common/lilith_shm.h"
}
struct PortalShortcut {QString id;QVariantMap options;};
using PortalShortcuts=QList<PortalShortcut>;
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)
QDBusArgument &operator<<(QDBusArgument &a,const PortalShortcut &s) {a.beginStructure();a<<s.id<<s.options;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,PortalShortcut &s) {a.beginStructure();a>>s.id>>s.options;a.endStructure();return a;}
static const QString SERVICE="org.freedesktop.portal.Desktop";
static const QString PATH="/org/freedesktop/portal/desktop";
static const QString IFACE="org.freedesktop.portal.GlobalShortcuts";
static const char *LABELS[]={"Lilith: Music","Lilith: Alarm","Lilith: Timer","Lilith: Pomodoro","Lilith: Simple memo","Lilith: Detailed memo","Lilith: Scratch memo","Lilith: Mute","Lilith: Click-through"};
// Portal preferred_trigger uses XDG accelerator syntax, not Qt's portable text.
static QString trigger(uint32_t mods,uint32_t vk) {
    QString k;
    if((vk>='A' && vk<='Z') || (vk>='0' && vk<='9'))k=QChar(vk);
    else if(vk>=0x70 && vk<=0x87)k=QString("F%1").arg(vk-0x70+1);
    else switch(vk) {
    case 8:k="BackSpace";break;case 9:k="Tab";break;case 13:k="Return";break;
    case 27:k="Escape";break;case 32:k="space";break;
    case 33:k="Page_Up";break;case 34:k="Page_Down";break;case 35:k="End";break;case 36:k="Home";break;
    case 37:k="Left";break;case 38:k="Up";break;case 39:k="Right";break;case 40:k="Down";break;
    case 45:k="Insert";break;case 46:k="Delete";break;
    case 186:k="semicolon";break;case 187:k="equal";break;case 188:k="comma";break;
    case 189:k="minus";break;case 190:k="period";break;case 191:k="slash";break;
    case 192:k="grave";break;case 219:k="bracketleft";break;case 220:k="backslash";break;
    case 221:k="bracketright";break;case 222:k="apostrophe";break;
    default:return {};
    }
    if(!mods || (mods&~15u))return {}; // Never globally reserve bare typing keys.
    QString p;if(mods&1)p+="CTRL+";if(mods&2)p+="ALT+";if(mods&4)p+="SHIFT+";if(mods&8)p+="LOGO+";
    return p+k;
}
class GlobalShortcuts final:public QObject {
    Q_OBJECT
    ll_header *h;QDBusConnection bus=QDBusConnection::sessionBus();
    QTimer poll,timeout;QElapsedTimer clock;
    QString session,pending;int phase=0;bool busy=false,disabled=false,refused=false,identity=false;uint64_t epoch=0;
    uint32_t mods[LL_HOTKEY_COUNT]={},keys[LL_HOTKEY_COUNT]={};
    QSet<QString> bound,held; qint64 lastFire[LL_HOTKEY_COUNT]={};
    QString token() {return "lilith_"+QUuid::createUuid().toString(QUuid::Id128);}
    void closeSession() {
        h->hotkey_status=0;bound.clear();held.clear();
        if(!session.isEmpty()) {
            auto m=QDBusMessage::createMethodCall(SERVICE,session,"org.freedesktop.portal.Session","Close");
            bus.asyncCall(m);session.clear();
        }
    }
    void fail(const QString &reason) {
        fprintf(stderr,"hotkeys: %s; global shortcuts unavailable (no keyboard-grab fallback)\n",qPrintable(reason));
        timeout.stop();busy=false;disabled=true;pending.clear();closeSession();h->hotkey_status=2;
    }
    void request(const QString &method,QVariantList args,int next) {
        phase=next;busy=true;const uint64_t serial=++epoch;
        const QString t=token();
        QString sender=bus.baseService().mid(1);sender.replace('.','_');
        pending="/org/freedesktop/portal/desktop/request/"+sender+"/"+t;
        bus.connect(SERVICE,pending,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
        QVariantMap options;
        if(method=="CreateSession")options.insert("session_handle_token",token());
        options.insert("handle_token",t);args.append(options);
        auto m=QDBusMessage::createMethodCall(SERVICE,PATH,IFACE,method);m.setArguments(args);
        auto *watch=new QDBusPendingCallWatcher(bus.asyncCall(m),this);
        connect(watch,&QDBusPendingCallWatcher::finished,this,[this,watch,next,serial] {
            QDBusPendingReply<QDBusObjectPath> r=*watch;watch->deleteLater();
            if(!busy || phase!=next || serial!=epoch)return;
            if(r.isError()){fail(r.error().message());return;}
            if(r.value().path()!=pending && busy) {
                bus.disconnect(SERVICE,pending,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
                pending=r.value().path();
                bus.connect(SERVICE,pending,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
            }
        });
        timeout.start(180000); // User may need to approve the desktop's registration dialog.
    }
    void bind() {
        PortalShortcuts list;
        for(int i=0;i<LL_HOTKEY_COUNT;i++) {
            const QString key=trigger(mods[i],keys[i]);if(key.isEmpty())continue;
            list.append({QString::number(i),{{"description",QString::fromUtf8(LABELS[i])},{"preferred_trigger",key}}});
            fprintf(stderr,"hotkeys: request action=%d preferred=%s\n",i,qPrintable(key));
        }
        if(list.isEmpty()){busy=false;h->hotkey_status=0;return;}
        request("BindShortcuts",{QVariant::fromValue(QDBusObjectPath(session)),QVariant::fromValue(list),QString()},2);
    }
public:
    explicit GlobalShortcuts(ll_header *header,QObject *parent):QObject(parent),h(header) {
        qDBusRegisterMetaType<PortalShortcut>();qDBusRegisterMetaType<PortalShortcuts>();clock.start();
        for(auto &v:lastFire)v=-1000;
        auto *watcher=new QDBusServiceWatcher(SERVICE,bus,QDBusServiceWatcher::WatchForOwnerChange,this);
        connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString&,const QString&,const QString &owner){
            ++epoch;timeout.stop();busy=false;pending.clear();session.clear();held.clear();bound.clear();identity=false;h->hotkey_status=0;
            QCoreApplication::instance()->setProperty("lilith.portal.identity",false);
            if(!owner.isEmpty())QTimer::singleShot(300,this,[this]{recover();});
        });
        if(!bus.isConnected()){fail("session bus unavailable");return;}
        // Unsandboxed child processes otherwise inherit the launching terminal's app ID.
        // Register BEFORE any portal call so KDE never stores Lilith shortcuts under Konsole.
        auto registration=QDBusMessage::createMethodCall(SERVICE,PATH,"org.freedesktop.host.portal.Registry","Register");
        registration.setArguments({QString("org.lilith.LinuxBridge"),QVariantMap{}});
        auto registered=bus.call(registration,QDBus::Block,3000);
        if(registered.type()==QDBusMessage::ErrorMessage){fail("application identity registration failed: "+registered.errorMessage());return;}
        identity=true;
        fprintf(stderr,"hotkeys: portal application identity org.lilith.LinuxBridge registered\n");
        QCoreApplication::instance()->setProperty("lilith.portal.identity",true);
        if(qEnvironmentVariable("LILITH_LINUX_HOTKEYS")=="0") {disabled=true;fprintf(stderr,"hotkeys: disabled by environment\n");return;}
        bus.connect(SERVICE,PATH,IFACE,"Activated",this,SLOT(activated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        bus.connect(SERVICE,PATH,IFACE,"Deactivated",this,SLOT(deactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        connect(&timeout,&QTimer::timeout,this,[this]{fail("portal request timed out; restart Lilith to retry");});timeout.setSingleShot(true);
        connect(&poll,&QTimer::timeout,this,[this]{refresh();});poll.start(200);
    }
    void recover(){
        held.clear();for(auto &v:lastFire)v=-1000;
        if(refused || qEnvironmentVariable("LILITH_LINUX_HOTKEYS")=="0")return;
        if(!session.isEmpty() && h->hotkey_status==1)return;
        if(!identity){auto registration=QDBusMessage::createMethodCall(SERVICE,PATH,"org.freedesktop.host.portal.Registry","Register");registration.setArguments({QString("org.lilith.LinuxBridge"),QVariantMap{}});auto r=bus.call(registration,QDBus::Block,1000);if(r.type()==QDBusMessage::ErrorMessage)return;identity=true;QCoreApplication::instance()->setProperty("lilith.portal.identity",true);}
        disabled=false;busy=false;timeout.stop();pending.clear();closeSession();for(int i=0;i<LL_HOTKEY_COUNT;i++){mods[i]=keys[i]=0;}
        if(!poll.isActive()){connect(&poll,&QTimer::timeout,this,[this]{refresh();});poll.start(200);}refresh();
    }
    ~GlobalShortcuts() override {
        if(!pending.isEmpty())bus.asyncCall(QDBusMessage::createMethodCall(SERVICE,pending,"org.freedesktop.portal.Request","Close"));
        closeSession();
    }
    void refresh() {
        if(disabled || busy || h->magic!=LL_MAGIC || h->version!=LL_VERSION || !h->hotkey_ready || h->hotkey_capturing)return;
        uint32_t m[LL_HOTKEY_COUNT],k[LL_HOTKEY_COUNT];
        uint32_t seq=__atomic_load_n(&h->hotkey_seq,__ATOMIC_ACQUIRE);if(seq&1)return;
        for(int i=0;i<LL_HOTKEY_COUNT;i++){m[i]=h->hotkey_mods[i];k[i]=h->hotkey_keys[i];}
        __atomic_thread_fence(__ATOMIC_ACQUIRE);if(seq!=h->hotkey_seq)return;
        bool changed=false,any=false;
        for(int i=0;i<LL_HOTKEY_COUNT;i++) {changed|=m[i]!=mods[i] || k[i]!=keys[i];any|=!trigger(m[i],k[i]).isEmpty();}
        if(!changed)return;
        closeSession();
        for(int i=0;i<LL_HOTKEY_COUNT;i++){mods[i]=m[i];keys[i]=k[i];}
        if(any)request("CreateSession",{},1);
        else fprintf(stderr,"hotkeys: no supported saved bindings assigned\n");
    }
public slots:
    void response(uint code,QVariantMap results,QDBusMessage message) {
        if(message.path()!=pending || !busy)return;
        bus.disconnect(SERVICE,pending,"org.freedesktop.portal.Request","Response",this,SLOT(response(uint,QVariantMap,QDBusMessage)));
        pending.clear();timeout.stop();
        if(code!=0){refused=true;fail(QString("portal registration cancelled/denied (response %1)").arg(code));return;}
        if(phase==1) {
            QVariant v=results.value("session_handle");
            session=v.canConvert<QDBusObjectPath>()?v.value<QDBusObjectPath>().path():v.toString();
            if(!session.startsWith("/org/freedesktop/portal/desktop/session/")){fail("invalid portal session handle");return;}
            bind();return;
        }
        PortalShortcuts list=qdbus_cast<PortalShortcuts>(results.value("shortcuts"));
        for(const auto &s:list) {
            bool ok=false;int i=s.id.toInt(&ok);if(!ok || i<0 || i>=LL_HOTKEY_COUNT)continue;
            bound.insert(s.id);
            fprintf(stderr,"hotkeys: registered action=%d trigger=%s\n",i,qPrintable(s.options.value("trigger_description").toString()));
        }
        busy=false;h->hotkey_status=bound.isEmpty()?2:1;
        fprintf(stderr,"hotkeys: portal ready, %lld registered action(s)\n",(long long)bound.size());
    }
    void activated(QDBusObjectPath path,QString id,qulonglong,QVariantMap) {
        if(path.path()!=session || busy || !bound.contains(id) || held.contains(id) || h->hotkey_capturing)return;
        bool ok=false;int i=id.toInt(&ok);if(!ok || i<0 || i>=LL_HOTKEY_COUNT)return;
        held.insert(id);qint64 now=clock.elapsed();if(now-lastFire[i]<350)return;lastFire[i]=now;
        __atomic_add_fetch(&h->hotkey_fired[i],1,__ATOMIC_RELEASE);
        fprintf(stderr,"hotkeys: activated action=%d\n",i);
    }
    void deactivated(QDBusObjectPath path,QString id,qulonglong,QVariantMap) {if(path.path()==session)held.remove(id);}
};
QObject *ll_start_hotkeys(ll_header *header,QObject *parent) {return new GlobalShortcuts(header,parent);}
#include "global_shortcuts.moc"

void ll_recover_hotkeys(QObject *object){if(auto *bridge=dynamic_cast<GlobalShortcuts*>(object))bridge->recover();}
