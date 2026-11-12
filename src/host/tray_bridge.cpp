// Native StatusNotifier/XEmbed presentation. No Win32 menus, windows or balloon notifications.
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QIcon>
#include <QImage>
#include <QFileInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>
#include <QHash>
#include <QVector>
#include <memory>
#include <cstring>
#include <cstdio>
#include "../common/lilith_shm.h"
class LilithTray:public QObject {
public:
    ll_tray *channel;
    QSystemTrayIcon mainIcon,badgeIcon;
    QMenu menu;
    QTimer timer,blink;
    std::unique_ptr<ll_tray_snapshot> snapshot{new ll_tray_snapshot{}},incoming{new ll_tray_snapshot{}};
    uint32_t seen=0xffffffffu,badgeLatch=0;bool testing=false,lastAvailable=false,blinkOff=false;
    QIcon mainArt,badgeArt,blankArt;
    QString mainPath,badgePath;
    QVector<QDBusPendingCallWatcher*> notifications;
    explicit LilithTray(ll_header *h,QObject *parent=nullptr,bool test=false):QObject(parent),
        channel(reinterpret_cast<ll_tray*>(reinterpret_cast<char*>(h)+LL_TRAY_OFF)),testing(test) {
        mainIcon.setContextMenu(&menu);
        QImage blank(32,32,QImage::Format_ARGB32_Premultiplied);blank.fill(Qt::transparent);blankArt=QIcon(QPixmap::fromImage(blank));
        QObject::connect(&badgeIcon,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason why){
            if(why==QSystemTrayIcon::Trigger && !badgeLatch && snapshot->badge_visible) {
                if(enqueue(LL_TRAY_BADGE_ID,snapshot->generation))badgeLatch=1;
            }
        });
        QObject::connect(&menu,&QMenu::aboutToHide,this,[this]{QTimer::singleShot(0,this,[this]{poll();});});
        QObject::connect(&timer,&QTimer::timeout,this,[this]{poll();});timer.start(40);
        QObject::connect(&blink,&QTimer::timeout,this,[this]{
            if(!snapshot->active || !snapshot->badge_visible || testing)return;
            blinkOff=!blinkOff;badgeIcon.setIcon(blinkOff?blankArt:badgeArt);
        });blink.setInterval(600);
    }
    ~LilithTray() override {
        timer.stop();blink.stop();mainIcon.hide();badgeIcon.hide();mainIcon.setContextMenu(nullptr);
        menu.close();menu.clear();
        if(channel->magic==LL_TRAY_MAGIC)__atomic_store_n(&channel->host_status,0,__ATOMIC_RELEASE);
        // Calls are asynchronous; deleting watchers disconnects replies without replaying notifications.
        for(auto *w:notifications)delete w;
    }
    static QString text(const uint16_t *s,size_t cap) {
        size_t n=0;while(n<cap && s[n])++n;
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(s),qsizetype(n));
    }
    bool readSnapshot(uint32_t &seq) {
        uint32_t a=__atomic_load_n(&channel->seq,__ATOMIC_ACQUIRE);if(a&1)return false;
        std::memcpy(incoming.get(),&channel->snapshot,sizeof(*incoming));__atomic_thread_fence(__ATOMIC_ACQUIRE);
        uint32_t b=__atomic_load_n(&channel->seq,__ATOMIC_ACQUIRE);if(a!=b || b&1)return false;
        if(incoming->count>LL_TRAY_ITEMS)return false;
        seq=b;return true;
    }
    bool enqueue(uint32_t id,uint32_t generation) {
        if(!snapshot->active || channel->magic!=LL_TRAY_MAGIC || generation!=snapshot->generation ||
           __atomic_load_n(&channel->host_status,__ATOMIC_ACQUIRE)!=1)return false;
        if(id!=LL_TRAY_BADGE_ID) {
            bool valid=false;for(uint32_t i=0;i<snapshot->count;i++)if(snapshot->items[i].id==id && snapshot->items[i].kind==0 && snapshot->items[i].enabled)valid=true;
            if(!valid)return false;
        } else if(!snapshot->badge_visible)return false;
        uint32_t w=channel->event_write,r=__atomic_load_n(&channel->event_read,__ATOMIC_ACQUIRE);
        if(w-r>=LL_TRAY_EVENTS){++channel->event_dropped;return false;}
        channel->events[w%LL_TRAY_EVENTS]={generation,id};__atomic_store_n(&channel->event_write,w+1,__ATOMIC_RELEASE);return true;
    }
    QIcon artwork(const QString &path,const QString &fallback) {
        QFileInfo fi(path);
        if(fi.isAbsolute() && fi.isFile() && fi.size()>0 && fi.size()<4*1024*1024) {
            QIcon result(path);if(!result.isNull())return result;
        }
        QIcon themed=QIcon::fromTheme(fallback);
        if(!themed.isNull())return themed;
        // Deterministic fallback only; the normal game-provided .ico remains preferred.
        QImage image(32,32,QImage::Format_ARGB32);image.fill(QColor(170,55,75));return QIcon(QPixmap::fromImage(image));
    }
    void rebuild() {
        menu.clear();QHash<uint32_t,QMenu*> parents;parents.insert(0,&menu);
        for(uint32_t i=0;i<snapshot->count;i++) {
            const auto &entry=snapshot->items[i];
            if(!entry.id || parents.contains(entry.id) || !parents.contains(entry.parent))continue;
            QMenu *target=parents.value(entry.parent);
            if(entry.kind==2){target->addSeparator();continue;}
            QString label=text(entry.label,LL_TRAY_LABEL);
            if(entry.kind==1){QMenu *sub=target->addMenu(label);parents.insert(entry.id,sub);continue;}
            if(entry.kind!=0)continue;
            QAction *action=target->addAction(label);action->setEnabled(entry.enabled!=0);
            const uint32_t id=entry.id,generation=snapshot->generation;
            QObject::connect(action,&QAction::triggered,this,[this,id,generation]{enqueue(id,generation);});
        }
        const QString path=text(snapshot->icon,LL_TRAY_PATH),note=text(snapshot->badge_icon,LL_TRAY_PATH);
        if(path!=mainPath || mainArt.isNull()){mainPath=path;mainArt=artwork(path,"applications-games");mainIcon.setIcon(mainArt);}
        if(note!=badgePath || badgeArt.isNull()){badgePath=note;badgeArt=artwork(note,"mail-unread");}
        mainIcon.setToolTip(text(snapshot->title,LL_TRAY_LABEL));badgeIcon.setToolTip(text(snapshot->badge_tip,LL_TRAY_LABEL));
        badgeIcon.setIcon(badgeArt);blinkOff=false;badgeLatch=0;
        fprintf(stderr,"tray: native menu generation=%u entries=%u icon_file=%d\n",snapshot->generation,snapshot->count,QFileInfo(mainPath).isFile());
    }
    void notify(const ll_tray_notification &n) {
        auto message=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify");
        QVariantMap hints{{"desktop-entry",QString("org.lilith.LinuxBridge")},{"urgency",QVariant::fromValue<uchar>(1)}};
        // Notification bodies support markup; original managed text is plain text.
        message.setArguments({QString("Lilith"),uint(0),mainPath.isEmpty()?QString("applications-games"):mainPath,
            text(n.title,LL_TRAY_LABEL),text(n.text,1024).toHtmlEscaped(),QStringList{},hints,int(qBound(1000u,n.duration,30000u))});
        auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message,10000),this);notifications.push_back(watcher);
        QObject::connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,watcher]{
            QDBusPendingReply<uint> reply=*watcher;
            if(reply.isError())fprintf(stderr,"tray: native notification error=%s\n",qPrintable(reply.error().name()));
            else fprintf(stderr,"tray: native notification accepted\n");
            notifications.removeOne(watcher);watcher->deleteLater();
        });
    }
    void poll() {
        if(channel->magic!=LL_TRAY_MAGIC)return;
        uint32_t seq=0;if(!readSnapshot(seq))return;
        bool available=testing || QSystemTrayIcon::isSystemTrayAvailable();
        __atomic_store_n(&channel->host_status,available?1:2,__ATOMIC_RELEASE);
        if(available!=lastAvailable){fprintf(stderr,"tray: native system tray available=%d\n",available);lastAvailable=available;}
        if(seq!=seen && !menu.isVisible()) {
            *snapshot=*incoming;seen=seq;rebuild();
        }
        if(!testing){
            mainIcon.setVisible(available && snapshot->active);
            badgeIcon.setVisible(available && snapshot->active && snapshot->badge_visible);
        }
        if(!testing && available && snapshot->active && snapshot->badge_visible) {if(!blink.isActive())blink.start();}
        else blink.stop();
        for(int i=0;i<LL_TRAY_NOTIFICATIONS;i++) {
            auto *n=&channel->notifications[i];uint32_t ready=2;
            if(!__atomic_compare_exchange_n(&n->state,&ready,3,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
            if(incoming->active && n->generation==incoming->generation)notify(*n);
            __atomic_store_n(&n->state,0,__ATOMIC_RELEASE);
        }
    }
};
QObject *ll_start_tray(ll_header *header,QObject *parent) {
    if(qEnvironmentVariable("LILITH_LINUX_TRAY")=="0")return nullptr;
    return new LilithTray(header,parent);
}
