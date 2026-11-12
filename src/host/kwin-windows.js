// Read-only window geometry feed. No captions, process arguments or content are collected.
(function () {
    var service='org.lilith.LinuxBridge', path='/LilithLinux', iface='org.lilith.LinuxBridge';
    var watched={}, pending=false, dirty=false, failed=false;
    function signal(object, name, callback) {
        try {if(object[name] && object[name].connect)object[name].connect(callback);} catch(e) {}
    }
    function relevant(w) {
        if(!w || w.minimized || w.deleted || w.hidden || workspace.showingDesktop)return false;
        if(!(w.normalWindow || w.dialog))return false;
        var cls=String(w.resourceClass||'').toLowerCase(),name=String(w.resourceName||'').toLowerCase();
        if(cls.indexOf('lilith')>=0 || name.indexOf('lilith')>=0 || cls==='steam_app_4643090' || name==='steam_app_4643090')return false;
        var desktop=workspace.currentDesktopForScreen ? workspace.currentDesktopForScreen(w.output) : workspace.currentDesktop;
        if(w.desktops && w.desktops.length && w.desktops.indexOf(desktop)<0)return false;
        if(w.activities && w.activities.length && w.activities.indexOf(workspace.currentActivity)<0)return false;
        return true;
    }
    function snapshot() {
        var windows=workspace.windowList().filter(relevant);
        windows.sort(function(a,b){return b.stackingOrder-a.stackingOrder;});
        var rows=[];
        for(var i=0;i<windows.length && rows.length<32;i++) {
            var w=windows[i],g=w.frameGeometry;
            if(!g || g.width<32 || g.height<24)continue;
            rows.push({id:String(w.internalId),x:Math.round(g.x),y:Math.round(g.y),w:Math.round(g.width),h:Math.round(g.height),flags:((w.fullScreen || w.maximizeMode===3)?1:0)});
        }
        return JSON.stringify(rows);
    }
    function publish() {
        dirty=true;if(pending || failed)return;
        dirty=false;pending=true;
        try {
            callDBus(service,path,iface,'publish',snapshot(),function(){pending=false;if(dirty)publish();});
            var active=workspace.activeWindow;
            callDBus(service,path,iface,'context',JSON.stringify({pid:active?Number(active.pid||0):0,cls:active?String(active.resourceClass||''):'',desktop:active?String(active.desktopFileName||''):'',fullscreen:!!(active && active.fullScreen)}),function(){});
        } catch(e) {pending=false;failed=true;print('lilith-linux: geometry feed failed');}
    }
    function watch(w) {
        var key=String(w.internalId);if(watched[key])return;watched[key]=true;
        ['frameGeometryChanged','minimizedChanged','desktopsChanged','activitiesChanged','fullScreenChanged','maximizedChanged','stackingOrderChanged','hiddenChanged','outputChanged'].forEach(function(name){signal(w,name,publish);});
    }
    signal(workspace,'windowAdded',function(w){watch(w);publish();});
    signal(workspace,'windowRemoved',function(w){delete watched[String(w.internalId)];publish();});
    ['stackingOrderChanged','currentDesktopChanged','currentActivityChanged','showingDesktopChanged','windowActivated','screensChanged'].forEach(function(name){signal(workspace,name,publish);});
    workspace.windowList().forEach(watch);publish();
})();
