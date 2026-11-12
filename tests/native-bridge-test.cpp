// Offscreen + PRIVATE bus only. Never use the desktop clipboard, real player or actual autostart directory.
#include "../src/host/native_bridge.cpp"
#include <QDBusVirtualObject>
#include <QTemporaryDir>
#include <QEventLoop>
#include <sys/mman.h>
#include <cassert>
static int resumes=0;void ll_native_recovery(){resumes++;}
class FakePlayer:public QDBusVirtualObject {
public:
    QStringList calls;
    QString introspect(const QString&)const override{return {};}
    bool handleMessage(const QDBusMessage &m,const QDBusConnection &b)override{
        if(m.interface()=="org.freedesktop.DBus.Properties" && m.member()=="Get"){
            QString key=m.arguments()[1].toString();QString value=key=="Identity"?"Fixture Player":"Playing";
            b.send(m.createReply(QVariant::fromValue(QDBusVariant(value))));return true;
        }
        calls<<m.member();b.send(m.createReply());return true;
    }
};
static void events(int ms=100){QElapsedTimer t;t.start();while(t.elapsed()<ms)QCoreApplication::processEvents(QEventLoop::AllEvents,10);}
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QTemporaryDir temp;assert(temp.isValid());
    auto bus=QDBusConnection::sessionBus();FakePlayer fake;assert(bus.registerService("org.mpris.MediaPlayer2.fixture"));assert(bus.registerVirtualObject("/org/mpris/MediaPlayer2",&fake));
    auto *memory=mmap(nullptr,LL_FILE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory!=MAP_FAILED);
    auto *h=(ll_header*)memory;auto *n=nativeChannel(h);n->magic=LL_NATIVE_MAGIC;
    {
        LilithNative native(h,nullptr,true);native.timer.stop();native.playersTimer.stop();native.autostartPath=temp.path()+"/config/autostart/org.lilith.LinuxBridge.desktop";
        native.launcher=temp.path()+"/launcher с пробелами.sh";QFile launcher(native.launcher);assert(launcher.open(QIODevice::WriteOnly));launcher.write("#!/bin/sh\nexit 0\n");launcher.close();assert(launcher.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        assert(!native.autostartEnabled());assert(native.setAutostart(true));assert(native.autostartEnabled());
        QFile entry(native.autostartPath);assert(entry.open(QIODevice::ReadOnly));QByteArray before=entry.readAll();entry.close();assert(before.contains("start -autostart") && before.contains("X-Lilith-Managed=true"));
        assert(native.setAutostart(false));assert(!QFileInfo::exists(native.autostartPath));
        assert(entry.open(QIODevice::WriteOnly));entry.write("UNMANAGED\n");entry.close();assert(!native.setAutostart(true));assert(!native.setAutostart(false));
        assert(entry.open(QIODevice::ReadOnly));assert(entry.readAll()=="UNMANAGED\n");entry.close();entry.remove();
        QMimeData mime;QString path=temp.path()+QString::fromUtf8("/Музыка с пробелами.ogg");QFile file(path);assert(file.open(QIODevice::WriteOnly));file.write("fixture");file.close();
        mime.setUrls({QUrl::fromLocalFile(path)});mime.setData("application/x-kde-cutselection","1");
        auto urls=LilithNative::fileUrls(&mime);assert(urls.size()==1 && LilithNative::cutFiles(&mime));
        assert(LilithNative::fillFiles(&n->drops[0],urls,true,false,{100,200}));assert(nativeText(n->drops[0].paths,LL_NATIVE_FILE_UNITS)==path);assert(n->drops[0].count==1 && n->drops[0].cut==1 && n->drops[0].x==100);
        assert(!LilithNative::fillFiles(&n->drops[1],{QUrl("https://example.com/file")},false,true));
        assert(!LilithNative::fillFiles(&n->drops[1],{QUrl::fromLocalFile("/"+QString(4096,'a'))},false,true));
        n->drops[0].state=0;assert(native.drop(&mime,{55,66},true));assert(n->drops[0].state==2 && n->drops[0].copy==1);n->drops[0].state=0;
        // Offscreen QClipboard contains only this synthetic test MIME.
        auto *owned=new QMimeData;owned->setUrls(urls);owned->setData("application/x-kde-cutselection","1");QApplication::clipboard()->setMimeData(owned);
        native.clipboardMetadata();assert(n->clipboard_has_files);n->clipboard.state=1;native.clipboardRequests();assert(n->clipboard.state==4 && n->clipboard.cut==1 && n->clipboard.token);
        uint32_t token=n->clipboard.token;QApplication::clipboard()->setText("newer clipboard must survive");n->clipboard.state=0;n->clipboard_clear=token;native.clipboardRequests();assert(QApplication::clipboard()->text()=="newer clipboard must survive");
        auto *owned2=new QMimeData;owned2->setUrls(urls);QApplication::clipboard()->setMimeData(owned2);n->clipboard.state=1;native.clipboardRequests();n->clipboard_clear=n->clipboard.token;n->clipboard.state=0;native.clipboardRequests();assert(!QApplication::clipboard()->mimeData() || !QApplication::clipboard()->mimeData()->hasUrls());
        native.context("fixture-process","Fixture App",true,true);native.poll();assert(n->snapshot.context_ready && n->snapshot.fullscreen && n->snapshot.idle_ms==1234);
        native.screenLocked(true);native.poll();assert(n->snapshot.locked);uint32_t reset=h->input_reset;native.screenLocked(false);assert(h->input_reset==reset+1 && n->resume_serial==1 && resumes==1);
        native.scanPlayers();events();native.poll();assert(n->snapshot.player_count==1);assert(nativeText(n->snapshot.players[0].id)=="org.mpris.MediaPlayer2.fixture");assert(nativeText(n->snapshot.players[0].label)=="Fixture Player");
        for(uint32_t kind=2;kind<=6;kind++){auto &c=n->commands[0];c.kind=kind;nativePut(c.target,LL_NATIVE_TEXT,"org.mpris.MediaPlayer2.fixture");c.state=2;native.commands();events();assert(c.state==0);}
        // Repeated stop is Pause again, NEVER a play/pause toggle.
        auto &stop=n->commands[0];stop.kind=6;stop.state=2;native.commands();events();
        assert(fake.calls.takeLast()=="Pause");
        // If the player we launched disappears, never stop an unrelated fallback.
        native.players={"org.mpris.MediaPlayer2.someOtherPlayer"};stop.kind=6;stop.state=2;native.commands();events();
        assert(fake.calls==QStringList({"Raise","Play","PlayPause","Next","Previous","Pause"}));
    }
    munmap(memory,LL_FILE_SIZE);puts("PASS: isolated atomic autostart/unmanaged-file protection; Unicode local files/cut/copy/bounds; clipboard change protection; targeted drop queue; desktop context; resume reset; MPRIS discovery and exact commands");
}
