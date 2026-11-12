#!/usr/bin/env python3
"""Install only the bridge DLL and a managed desktop entry; preserve existing files."""
import argparse,json,os,shutil,sys,time
from pathlib import Path
from bridge_config import ROOT,add_arguments,make_config,config_path,state_dir,atomic_json,digest,desktop_entry,load_config

def ensure_closed(game_dir):
    game_dir=Path(game_dir).resolve()
    for p in Path('/proc').glob('[0-9]*'):
        try:
            if (p/'comm').read_text().strip()=='Lilith.exe' and (p/'cwd').resolve()==game_dir:
                raise ValueError('Close Lilith before installing/uninstalling; an instance is still using this game directory.')
        except (OSError,RuntimeError):continue

def restore(old,target):
    atomic_copy(old['path'],target)
    if 'mode' in old:os.chmod(target,old['mode'])

def desktop_path():return Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))/'applications/org.lilith.LinuxBridge.desktop'
def record_path():return state_dir()/'installation.json'
def atomic_copy(source,target):
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True);tmp=target.with_name(target.name+'.lilith-tmp')
    if tmp.exists() or tmp.is_symlink():raise ValueError('Unexpected temporary file; refusing to overwrite '+str(tmp))
    try:shutil.copy2(source,tmp);os.replace(tmp,target)
    finally:
        if tmp.exists():tmp.unlink()

def backup(file,folder,label):
    if file.is_symlink():raise ValueError('Refusing to replace symbolic link: '+str(file))
    if not file.exists():return None
    dest=folder/label;dest.parent.mkdir(parents=True,exist_ok=True,mode=0o700);shutil.copy2(file,dest);os.chmod(dest,0o600)
    return {'path':str(dest),'sha256':digest(dest),'mode':file.stat().st_mode&0o777}

def uninstall(dry_run=False):
    file=record_path()
    if not file.is_file():raise ValueError('No managed installation record found')
    rec=json.loads(file.read_text());ensure_closed(rec['game_dir']);targets=[(Path(rec['game_dir'])/'version.dll',rec['proxy']), (Path(rec['desktop_path']),rec['desktop'])]
    # Preflight EVERYTHING before touching either file.
    for target,item in targets:
        if target.is_symlink() or (target.exists() and digest(target)!=item['installed_sha256']):raise ValueError('File changed since installation; refusing to overwrite/remove: '+str(target))
        old=item.get('backup')
        if old and (not Path(old['path']).is_file() or digest(old['path'])!=old['sha256']):raise ValueError('Backup missing or changed; restore manually: '+str(target))
    print('Restore/remove the managed version.dll and desktop entry; preserve Steam prefix/saves.')
    if dry_run:return
    for target,item in targets:
        if item.get('backup'):restore(item['backup'],target)
        elif target.exists():target.unlink()
    # Remove autostart only when it explicitly belongs to THIS bridge root.
    auto=Path(os.environ.get('XDG_CONFIG_HOME',Path.home()/'.config'))/'autostart/org.lilith.LinuxBridge.desktop'
    if auto.is_file() and not auto.is_symlink():
        content=auto.read_text()
        if 'X-Lilith-Managed=true' in content and str(Path(rec['bridge_root'])/'launch.sh') in content:auto.unlink()
    file.unlink();print('Uninstalled. Backups and local configuration retained in the per-user state/config directories.')

def install(args):
    if args.uninstall:return uninstall(args.dry_run)
    config=make_config(args);ensure_closed(config['game_dir']);payload=ROOT/'build/version.dll';host=ROOT/'build/lilith-host';layout=ROOT/'build/shm-layout.json'
    for p in (payload,host,layout):
        if not p.is_file():raise ValueError('Build missing: '+str(p))
    if not os.access(host,os.X_OK):raise ValueError('Native host is not executable')
    target=Path(config['game_dir'])/'version.dll';desk=desktop_path();record=record_path()
    previous=json.loads(record.read_text()) if record.is_file() else None
    if previous and previous['game_dir']!=config['game_dir']:raise ValueError('A different game directory is registered; uninstall it first')
    expected={'proxy':target,'desktop':desk}
    for kind,path in expected.items():
        if path.is_symlink():raise ValueError('Refusing symbolic link: '+str(path))
        owned=previous and path.is_file() and digest(path)==previous[kind]['installed_sha256']
        if path.exists() and not owned and not args.replace_existing:raise ValueError('Existing unmanaged file: '+str(path)+'\nUse --replace-existing to explicitly back it up and replace it.')
    if record.is_file() and not previous:raise ValueError('Invalid installation record')
    print(json.dumps({'action':'install','game_dir':config['game_dir'],'proton':config['proton'],'verified_game_build':config['verified_game_build'],'desktop_entry':str(desk),'autostart':'unchanged/off unless enabled in the app'},ensure_ascii=False,indent=2))
    if args.dry_run:return
    folder=state_dir()/'backups'/str(time.time_ns());folder.mkdir(parents=True,mode=0o700)
    snapshots={kind:backup(path,folder,'rollback-'+kind) for kind,path in expected.items()}
    old_config=config_path().read_bytes() if config_path().exists() else None
    old_record=record.read_bytes() if record.exists() else None
    entries={}
    for kind,path in expected.items():
        # Updates keep the first pre-bridge backup, not another copy of our DLL.
        owned=previous and path.is_file() and digest(path)==previous[kind]['installed_sha256']
        entries[kind]={'backup':previous[kind].get('backup') if owned else snapshots[kind]}
    try:
        atomic_copy(payload,target)
        text=folder/'desktop-entry.new';text.write_text(desktop_entry(ROOT),encoding='utf-8');os.chmod(text,0o644);atomic_copy(text,desk)
        for kind,path in expected.items():entries[kind]['installed_sha256']=digest(path)
        atomic_json(config_path(),config)
        atomic_json(record,{'schema':1,'bridge_root':str(ROOT),'game_dir':config['game_dir'],'desktop_path':str(desk),**entries})
    except BaseException:
        for kind,path in expected.items():
            if snapshots[kind]:restore(snapshots[kind],path)
            elif path.exists():path.unlink()
        if old_config is not None:config_path().write_bytes(old_config)
        elif config_path().exists():config_path().unlink()
        if old_record is not None:record.write_bytes(old_record)
        elif record.exists():record.unlink()
        raise
    print('Installed. Keep Steam running and start Lilith Linux Bridge from the application menu or ./launch.sh start.')
    print('Do not move this bridge directory after installing. Autostart was NOT enabled.')

def main():
    p=argparse.ArgumentParser(description=__doc__);add_arguments(p)
    p.add_argument('--dry-run',action='store_true');p.add_argument('--replace-existing',action='store_true',help='Explicitly back up/replace existing unmanaged proxy or desktop entry');p.add_argument('--uninstall',action='store_true')
    args=p.parse_args()
    try:install(args)
    except (OSError,ValueError,KeyError) as e:p.exit(1,str(e)+'\n')
if __name__=='__main__':main()
