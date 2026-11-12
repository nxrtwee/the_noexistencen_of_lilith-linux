// KDE-native parity services. No GNOME installation, global key hook or clipboard text capture.
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusServiceWatcher>
#include <QStandardPaths>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QCryptographicHash>
#include <QSettings>
#include <KIdleTime>
#include <memory>
#include <cstring>
#include <cstdio>
#include "../common/lilith_shm.h"
static ll_native *nativeChannel(ll_header *h){return reinterpret_cast<ll_native*>(reinterpret_cast<char*>(h)+LL_NATIVE_OFF);}
static QString nativeText(const uint16_t *s,int cap=LL_NATIVE_TEXT){int n=0;while(n<cap && s[n])n++;return QString::fromUtf16(reinterpret_cast<const char16_t*>(s),n);}
static void nativePut(uint16_t *dst,int cap,const QString &text){int n=qMin(cap-1,int(text.size()));if(n && text[n-1].isHighSurrogate())n--;std::memcpy(dst,text.utf16(),n*2);dst[n]=0;}
void ll_native_recovery();
class LilithNative final:public QObject {
    Q_OBJECT
public:
    ll_header *header;ll_native *channel;QTimer timer,playersTimer;QElapsedTimer clock;
    QDBusConnection bus=QDBusConnection::sessionBus();
    QString autostartPath,launcher,controlledPlayer;QStringList players;QHash<QString,QString> names,status;
    QByteArray lastClipboard;uint32_t clipboardToken=0;bool playerScan=false,testing=false;
    bool contextReady=false,full=false,locked=false;QString process,app;
    explicit LilithNative(ll_header *h,QObject *parent=nullptr,bool test=false):QObject(parent),header(h),channel(nativeChannel(h)),testing(test) {
        launcher=QFileInfo(QCoreApplication::applicationDirPath()+"/../launch.sh").absoluteFilePath();
        autostartPath=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/autostart/org.lilith.LinuxBridge.desktop";
        clock.start();connect(&timer,&QTimer::timeout,this,[this]{poll();});timer.start(40);
        connect(&playersTimer,&QTimer::timeout,this,[this]{scanPlayers();});playersTimer.start(3000);
        if(!testing){
            connect(QApplication::clipboard(),&QClipboard::dataChanged,this,[this]{clipboardMetadata();});
            KIdleTime::instance();scanPlayers();
            QDBusConnection::systemBus().connect("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager","PrepareForSleep",this,SLOT(sleepChanged(bool)));
            bus.connect("org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver","ActiveChanged",this,SLOT(screenLocked(bool)));
            QTimer::singleShot(0,this,[this]{auto m=QDBusMessage::createMethodCall("org.freedesktop.ScreenSaver","/ScreenSaver","org.freedesktop.ScreenSaver","GetActive");auto *w=new QDBusPendingCallWatcher(bus.asyncCall(m),this);connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{QDBusPendingReply<bool> r=*w;if(!r.isError())locked=r.value();w->deleteLater();});});
        }
    }
    bool managedAutostart()const {QFile f(autostartPath);return f.open(QIODevice::ReadOnly) && f.readAll().contains("X-Lilith-Managed=true");}
    bool autostartEnabled()const {QFile f(autostartPath);if(!f.open(QIODevice::ReadOnly))return false;QByteArray d=f.readAll();return d.contains("X-Lilith-Managed=true") && !d.contains("Hidden=true");}
    bool setAutostart(bool enabled) {
        if(QFileInfo::exists(autostartPath) && !managedAutostart()){fprintf(stderr,"native: refusing to replace unmanaged autostart file\n");return false;}
        if(!enabled)return !QFileInfo::exists(autostartPath) || QFile::remove(autostartPath);
        if(!QFileInfo(launcher).isExecutable() || !QDir().mkpath(QFileInfo(autostartPath).absolutePath()))return false;
        QString exec=launcher;exec.replace('%',"%%");exec.replace('\\',"\\\\");exec.replace('"',"\\\"");exec.replace('`',"\\`");exec.replace('$',"\\$");
        QByteArray data=("[Desktop Entry]\nType=Application\nName=Lilith\nComment=Lilith Linux desktop assistant\nExec=\""+exec+"\" start -autostart\nIcon=applications-games\nTerminal=false\nStartupNotify=false\nX-Lilith-Managed=true\n").toUtf8();
        QSaveFile f(autostartPath);if(!f.open(QIODevice::WriteOnly) || f.write(data)!=data.size())return false;return f.commit();
    }
    static QList<QUrl> fileUrls(const QMimeData *mime) {
        if(!mime || !mime->hasUrls())return {};auto urls=mime->urls();
        if(urls.isEmpty() || urls.size()>LL_NATIVE_FILE_COUNT)return {};
        for(const auto &url:urls)if(!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isAbsolute())return {};
        return urls;
    }
    static bool cutFiles(const QMimeData *mime) {
        return mime && ((mime->hasFormat("application/x-kde-cutselection") && mime->data("application/x-kde-cutselection")=="1") ||
            (mime->hasFormat("x-special/gnome-copied-files") && mime->data("x-special/gnome-copied-files").startsWith("cut\n")));
    }
    static QByteArray fingerprint(const QMimeData *mime){QByteArray data;for(const auto &u:fileUrls(mime)){data+=u.toEncoded();data+='\0';}data+=cutFiles(mime)?'1':'0';return QCryptographicHash::hash(data,QCryptographicHash::Sha256);}
    static bool fillFiles(ll_native_files *out,const QList<QUrl> &urls,bool cut,bool copy,QPoint point={}) {
        if(urls.isEmpty() || urls.size()>LL_NATIVE_FILE_COUNT)return false;uint32_t n=0;
        // Validate full batch first: no silent partial paste or drop.
        for(const auto &url:urls){QString path=url.toLocalFile();if(!url.isLocalFile() || !QFileInfo(path).isAbsolute() || path.size()>=4096 || n+path.size()+1>LL_NATIVE_FILE_UNITS)return false;n+=path.size()+1;}
        out->count=urls.size();out->units=n;out->cut=cut;out->copy=copy;out->x=point.x();out->y=point.y();n=0;
        for(int i=0;i<urls.size();i++){QString path=urls[i].toLocalFile();out->offsets[i]=n;nativePut(out->paths+n,LL_NATIVE_FILE_UNITS-n,path);n+=path.size()+1;}
        return true;
    }
    void clipboardMetadata(){if(channel->magic!=LL_NATIVE_MAGIC)return;const auto *m=QApplication::clipboard()->mimeData();channel->clipboard_has_files=m && m->hasUrls();}
    bool pasteFocused(){
        if(channel->magic!=LL_NATIVE_MAGIC || !channel->paste_enabled || !channel->paste_over || (header->game_flags&LL_KEYBOARD) || locked)return false;
        const auto *mime=QApplication::clipboard()->mimeData();if(fileUrls(mime).isEmpty())return false;
        __atomic_add_fetch(&channel->paste_fired,1,__ATOMIC_RELEASE);return true;
    }
    bool drop(const QMimeData *mime,QPoint point,bool copy){
        if(channel->magic!=LL_NATIVE_MAGIC || locked)return false;
        auto urls=fileUrls(mime);if(urls.isEmpty())return false;
        if(!testing){
            channel->drop_probe_x=point.x();channel->drop_probe_y=point.y();
            uint32_t serial=__atomic_add_fetch(&channel->drop_probe_seq,1,__ATOMIC_RELEASE);
            QElapsedTimer wait;wait.start();while(__atomic_load_n(&channel->drop_probe_reply,__ATOMIC_ACQUIRE)!=serial && wait.elapsed()<100)QThread::msleep(1);
            if(channel->drop_probe_reply!=serial || !channel->drop_probe_hit)return false;
        }
        for(auto &out:channel->drops){uint32_t free=0;if(!__atomic_compare_exchange_n(&out.state,&free,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
            bool ok=fillFiles(&out,urls,false,copy,point);__atomic_store_n(&out.state,ok?2:0,__ATOMIC_RELEASE);return ok;}
        return false;
    }
    void clipboardRequests(){
        auto &out=channel->clipboard;
        uint32_t request=1;
        if(__atomic_compare_exchange_n(&out.state,&request,3,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
            const auto *mime=QApplication::clipboard()->mimeData();bool ok=fillFiles(&out,fileUrls(mime),cutFiles(mime),false);
            if(ok){lastClipboard=fingerprint(mime);out.token=++clipboardToken;channel->clipboard_token=clipboardToken;}else out.count=out.units=0;
            __atomic_store_n(&out.state,4,__ATOMIC_RELEASE);
        }
        uint32_t token=__atomic_exchange_n(&channel->clipboard_clear,0,__ATOMIC_ACQ_REL);
        if(token && token==clipboardToken && !lastClipboard.isEmpty() && fingerprint(QApplication::clipboard()->mimeData())==lastClipboard){QApplication::clipboard()->clear();lastClipboard.clear();}
    }
    void scanPlayers(){
        if(playerScan)return;playerScan=true;auto m=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ListNames");
        auto *w=new QDBusPendingCallWatcher(bus.asyncCall(m),this);connect(w,&QDBusPendingCallWatcher::finished,this,[this,w]{QDBusPendingReply<QStringList> r=*w;w->deleteLater();playerScan=false;if(r.isError())return;
            QStringList next;for(const auto &n:r.value())if(n.startsWith("org.mpris.MediaPlayer2.") && next.size()<LL_NATIVE_PLAYERS)next<<n;next.sort();players=next;
            for(const auto &name:players){property(name,"org.mpris.MediaPlayer2","Identity");property(name,"org.mpris.MediaPlayer2.Player","PlaybackStatus");}
        });
    }
    void property(const QString &name,const QString &iface,const QString &key){
        auto m=QDBusMessage::createMethodCall(name,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","Get");m.setArguments({iface,key});
        auto *w=new QDBusPendingCallWatcher(bus.asyncCall(m,2000),this);connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,name,key]{QDBusPendingReply<QDBusVariant> r=*w;if(!r.isError()){if(key=="Identity")names[name]=r.value().variant().toString().left(255);else status[name]=r.value().variant().toString();}w->deleteLater();});
    }
    QString choosePlayer(QString preferred)const {if(players.contains(preferred))return preferred;for(const auto &p:players)if(status.value(p)=="Playing")return p;return players.value(0);}
    void mediaCall(const QString &service,const QString &iface,const QString &method){auto m=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2",iface,method);auto *w=new QDBusPendingCallWatcher(bus.asyncCall(m,3000),this);connect(w,&QDBusPendingCallWatcher::finished,this,[w]{QDBusPendingReply<> r=*w;if(r.isError())fprintf(stderr,"native: MPRIS command failed (%s)\n",qPrintable(r.error().name()));w->deleteLater();});}
    void launchDefaultPlayer(){
        auto *p=new QProcess(this);p->setProgram("xdg-mime");p->setArguments({"query","default","audio/mpeg"});
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p](int code,QProcess::ExitStatus){
            QString id=QString::fromUtf8(p->readAllStandardOutput()).trimmed();p->deleteLater();if(code || !id.endsWith(".desktop") || id.contains('/') || id.contains(".."))return;
            QString file=QStandardPaths::locate(QStandardPaths::GenericDataLocation,"applications/"+id);if(!file.isEmpty())QProcess::startDetached("gio",{"launch",file});
        });p->start();QTimer::singleShot(3000,p,[p]{if(p->state()!=QProcess::NotRunning)p->kill();});
    }
    void commands(){for(auto &c:channel->commands){uint32_t ready=2;if(!__atomic_compare_exchange_n(&c.state,&ready,3,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))continue;
        if(c.kind==1){bool ok=setAutostart(c.value!=0);fprintf(stderr,"native: autostart update enabled=%d success=%d\n",c.value!=0,ok);}
        else if(c.kind>=2 && c.kind<=6){
            QString selected=c.kind==2 || controlledPlayer.isEmpty()?choosePlayer(nativeText(c.target)):(players.contains(controlledPlayer)?controlledPlayer:QString());if(selected.isEmpty()){if(c.kind==2)launchDefaultPlayer();}
            else if(c.kind==2){controlledPlayer=selected;mediaCall(selected,"org.mpris.MediaPlayer2","Raise");mediaCall(selected,"org.mpris.MediaPlayer2.Player","Play");}
            else mediaCall(selected,"org.mpris.MediaPlayer2.Player",c.kind==3?"PlayPause":c.kind==4?"Next":c.kind==5?"Previous":"Pause");}
        __atomic_store_n(&c.state,0,__ATOMIC_RELEASE);
    }}
    void poll(){if(channel->magic!=LL_NATIVE_MAGIC)return;commands();if(!testing)clipboardRequests();
        auto &s=channel->snapshot;__atomic_add_fetch(&channel->seq,1,__ATOMIC_ACQ_REL);
        s.ready=1;s.autostart=autostartEnabled();s.context_ready=contextReady;s.fullscreen=full;s.locked=locked;
        int idle=testing?1234:KIdleTime::instance()->idleTime();s.idle_valid=idle>=0;s.idle_ms=qMax(0,idle);
        nativePut(s.process,LL_NATIVE_TEXT,process);nativePut(s.app,LL_NATIVE_TEXT,app);s.player_count=qMin(players.size(),LL_NATIVE_PLAYERS);
        for(uint32_t i=0;i<s.player_count;i++){nativePut(s.players[i].id,LL_NATIVE_TEXT,players[i]);nativePut(s.players[i].label,LL_NATIVE_TEXT,names.value(players[i],players[i].mid(23)));}
        __atomic_add_fetch(&channel->seq,1,__ATOMIC_RELEASE);
    }
    void context(QString name,QString label,bool fullscreen,bool valid){process=name;app=label;full=fullscreen;contextReady=valid;}
public slots:
    void screenLocked(bool value){locked=value;if(!value)recovered();}
    void recovered(){if(channel->magic==LL_NATIVE_MAGIC){++channel->resume_serial;++header->input_reset;for(auto &v:header->focused_vk)v=0;}scanPlayers();emitResume();}
    void sleepChanged(bool sleeping){if(!sleeping)recovered();}
    void emitResume(){ll_native_recovery();}
};
static LilithNative *nativeService=nullptr;
QObject *ll_start_native(ll_header *h,QObject *parent){nativeService=new LilithNative(h,parent);return nativeService;}
void ll_native_context(QString process,QString app,bool fullscreen,bool valid){if(nativeService)nativeService->context(process,app,fullscreen,valid);}
bool ll_native_paste(){return nativeService && nativeService->pasteFocused();}
bool ll_native_drop(const QMimeData *mime,QPoint point,bool copy){return nativeService && nativeService->drop(mime,point,copy);}
bool ll_native_paste_focus(ll_header *h){auto *n=nativeChannel(h);return n->magic==LL_NATIVE_MAGIC && n->paste_enabled && n->paste_over;}
#include "native_bridge.moc"

bool ll_native_locked(ll_header *h){auto *n=nativeChannel(h);return n->magic==LL_NATIVE_MAGIC && n->snapshot.locked;}
