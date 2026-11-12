// Run on a PRIVATE session bus: dbus-run-session -- build/global-shortcuts-test
#include "../src/host/global_shortcuts.cpp"
#include <QDBusVirtualObject>
#include <cassert>
class FakePortal:public QDBusVirtualObject {
public:
    QDBusConnection bus=QDBusConnection::sessionBus();
    QString session="/org/freedesktop/portal/desktop/session/test/lilith";
    int creates=0,binds=0,closes=0;
    QString introspect(const QString &) const override {return {};}
    bool handleMessage(const QDBusMessage &m,const QDBusConnection &b) override {
        if(m.member()=="Register"){assert(m.arguments()[0].toString()=="org.lilith.LinuxBridge");b.send(m.createReply());return true;}
        if(m.member()=="Close"){closes++;b.send(m.createReply());return true;}
        const auto args=m.arguments();const auto opts=qdbus_cast<QVariantMap>(args.last());
        QString sender=m.service().mid(1);sender.replace('.','_');
        QString path="/org/freedesktop/portal/desktop/request/"+sender+"/"+opts.value("handle_token").toString();
        b.send(m.createReply({QVariant::fromValue(QDBusObjectPath(path))}));
        QVariantMap results;
        if(m.member()=="CreateSession") {creates++;results.insert("session_handle",session);}
        else if(m.member()=="BindShortcuts") {
            binds++;auto list=qdbus_cast<PortalShortcuts>(args[1]);assert(list.size()==1);
            assert(list[0].options.value("preferred_trigger").toString()=="CTRL+ALT+M");
            list[0].options.insert("trigger_description","Ctrl+Alt+M");results.insert("shortcuts",QVariant::fromValue(list));
        } else {assert(false);}
        QTimer::singleShot(0,this,[this,path,results] {
            auto sig=QDBusMessage::createSignal(path,"org.freedesktop.portal.Request","Response");
            sig.setArguments({uint(0),results});bus.send(sig);
        });return true;
    }
    void activation(bool on,QString id="0",QString path={}) {
        auto sig=QDBusMessage::createSignal(PATH,IFACE,on?"Activated":"Deactivated");
        sig.setArguments({QVariant::fromValue(QDBusObjectPath(path.isEmpty()?session:path)),id,qulonglong(1),QVariantMap{}});bus.send(sig);
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    assert(trigger(3,77)=="CTRL+ALT+M");assert(trigger(12,0x7b)=="SHIFT+LOGO+F12");
    assert(trigger(0,65).isEmpty());assert(trigger(16,65).isEmpty());assert(trigger(3,0).isEmpty());
    ll_header h={};h.magic=LL_MAGIC;h.version=LL_VERSION;h.hotkey_ready=1;
    h.hotkey_mods[0]=3;h.hotkey_keys[0]=77;
    FakePortal portal;assert(portal.bus.registerService(SERVICE));
    assert(portal.bus.registerVirtualObject("/org/freedesktop/portal/desktop",&portal,QDBusConnection::SubPath));
    GlobalShortcuts client(&h,&app);
    auto wait=[&](int ms) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    wait(650);assert(h.hotkey_status==1);assert(portal.creates==1 && portal.binds==1);
    portal.activation(true);wait(40);assert(h.hotkey_fired[0]==1);
    portal.activation(true);wait(40);assert(h.hotkey_fired[0]==1); // held/repeated
    portal.activation(false);portal.activation(true);wait(40);assert(h.hotkey_fired[0]==1); // cooldown
    portal.activation(false);wait(400);
    h.hotkey_capturing=1;portal.activation(true);wait(40);assert(h.hotkey_fired[0]==1);
    h.hotkey_capturing=0;portal.activation(true,"99");portal.activation(true,"0","/wrong/session");wait(40);assert(h.hotkey_fired[0]==1);
    portal.activation(true);wait(40);assert(h.hotkey_fired[0]==2);
    portal.activation(false);h.hotkey_seq++;h.hotkey_keys[0]=0;h.hotkey_mods[0]=0;h.hotkey_seq++;
    wait(300);assert(h.hotkey_status==0);assert(portal.closes>=1);
    h.hotkey_seq++;h.hotkey_keys[0]=77;h.hotkey_mods[0]=3;h.hotkey_seq++;
    wait(400);assert(h.hotkey_status==1);
    assert(portal.bus.unregisterService(SERVICE));wait(80);assert(h.hotkey_status==0);
    assert(portal.bus.registerService(SERVICE));wait(1000);assert(h.hotkey_status==1);assert(portal.creates>=3 && portal.binds>=3);
    portal.activation(true);wait(50);assert(h.hotkey_fired[0]==3);
    puts("PASS: automatic reconnection after mock portal owner loss/reappearance");
    puts("PASS: portal wire signatures, session/bind/close, original binding export, activation delivery, hold/cooldown, capture gate, foreign/invalid IDs, clearing binding");
}
