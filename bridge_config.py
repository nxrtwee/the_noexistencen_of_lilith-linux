#!/usr/bin/env python3
"""Local Steam discovery and explicit per-user configuration; never modifies Steam settings."""
import hashlib, json, os, re, shlex
from pathlib import Path
APP_ID='4643090'
KNOWN_GAME_SHA256='e3c9dffd6fdcece3b6aa7a4f30e9de9235a2630122d04ab0654e532241b6205e'
ROOT=Path(__file__).resolve().parent

def config_path():
    return Path(os.environ.get('LILITH_CONFIG', str(Path(os.environ.get('XDG_CONFIG_HOME',Path.home()/'.config'))/'lilith-linux/config.json'))).expanduser()

def state_dir():
    return Path(os.environ.get('XDG_STATE_HOME',Path.home()/'.local/state'))/'lilith-linux'

def digest(path):
    with open(path,'rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def atomic_json(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    tmp=path.with_name(path.name+'.tmp')
    fd=os.open(tmp,os.O_WRONLY|os.O_CREAT|os.O_TRUNC|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'w',encoding='utf-8') as f:json.dump(data,f,ensure_ascii=False,indent=2);f.write('\n')
    os.replace(tmp,path)

def load_config(path=None):
    p=Path(path) if path else config_path()
    try:data=json.loads(p.read_text(encoding='utf-8'))
    except (OSError,ValueError) as e:raise ValueError('Configuration missing/invalid. Run python3 install.py first: '+str(e)) from e
    for key in ('game_dir','proton','steam_root','compat_data','game_sha256'):
        if not isinstance(data.get(key),str) or not data[key]:raise ValueError('Missing configuration key: '+key)
    return data

def parse_vdf(text):
    if len(text)>10_000_000:raise ValueError('Unexpectedly large Steam configuration')
    tokens=[]
    for m in re.finditer(r'//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])',text):
        if m.group(1) is not None:tokens.append(m.group(1).replace('\\"','"').replace('\\\\','\\'))
        elif m.group(2):tokens.append(m.group(2))
    def obj(pos,closing=False):
        out={}
        while pos<len(tokens):
            if tokens[pos]=='}':
                if not closing:raise ValueError('Unexpected closing brace in Steam configuration')
                return out,pos+1
            key=tokens[pos];pos+=1
            if pos>=len(tokens):raise ValueError('Incomplete Steam key/value')
            if tokens[pos]=='{':value,pos=obj(pos+1,True)
            else:value=tokens[pos];pos+=1
            out[key]=value
        if closing:raise ValueError('Unclosed Steam object')
        return out,pos
    return obj(0)[0]

def steam_roots(explicit=None):
    candidates=[Path(explicit).expanduser()] if explicit else [Path.home()/'.local/share/Steam',Path.home()/'.steam/steam',Path.home()/'.steam/root',Path.home()/'.var/app/com.valvesoftware.Steam/.local/share/Steam']
    return list(dict.fromkeys(p.resolve() for p in candidates if (p/'steamapps').is_dir()))

def installations(roots):
    out=[]
    for steam in roots:
        libraries=[steam]
        vdf=steam/'steamapps/libraryfolders.vdf'
        if vdf.is_file():
            for item in parse_vdf(vdf.read_text(encoding='utf-8')).get('libraryfolders',{}).values():
                path=item.get('path') if isinstance(item,dict) else item
                if isinstance(path,str):libraries.append(Path(path).expanduser().resolve())
        for library in dict.fromkeys(libraries):
            manifest=library/'steamapps'/('appmanifest_'+APP_ID+'.acf')
            if not manifest.is_file():continue
            app=parse_vdf(manifest.read_text(encoding='utf-8')).get('AppState',{})
            name=app.get('installdir')
            if not isinstance(name,str) or not name or Path(name).name!=name:continue
            game=(library/'steamapps/common'/name).resolve()
            if (game/'Lilith.exe').is_file():out.append({'game':game,'steam':steam,'library':library,'buildid':app.get('buildid','unknown')})
    return list({str(x['game']):x for x in out}.values())

def proton_candidates(roots,installs):
    dirs=[r/'compatibilitytools.d' for r in roots]+[x['library']/'steamapps/common' for x in installs]
    return sorted(set(p.resolve() for d in dirs if d.is_dir() for p in d.glob('*/proton') if p.is_file()),key=str)

def choose_proton(explicit,roots,installs):
    if explicit:
        p=Path(explicit).expanduser().resolve()
        if p.is_dir():p=p/'proton'
        if not p.is_file() or not os.access(p,os.X_OK):raise ValueError('Proton executable not found: '+str(p))
        return p
    choices=proton_candidates(roots,installs)
    # Use the game's explicit Steam compatibility mapping when it matches a tool directory.
    def mapping(obj):
        if not isinstance(obj,dict):return None
        for k,v in obj.items():
            if k.lower()=='compattoolmapping' and isinstance(v,dict):
                item=v.get(APP_ID,{})
                if isinstance(item,dict) and item.get('name'):return item['name']
            result=mapping(v)
            if result:return result
        return None
    for root in roots:
        file=root/'config/config.vdf'
        if file.is_file():
            name=mapping(parse_vdf(file.read_text(encoding='utf-8')))
            found=[p for p in choices if p.parent.name==name]
            if len(found)==1:return found[0]
    if len(choices)==1:return choices[0]
    raise ValueError('Select Proton explicitly with --proton /path/to/proton. Detected tools:\n'+'\n'.join(map(str,choices)))

def make_config(args):
    roots=steam_roots(args.steam_root);installs=installations(roots)
    game=Path(args.game_dir).expanduser().resolve() if args.game_dir else None
    matches=[x for x in installs if not game or x['game']==game]
    if not game:
        if len(matches)!=1:raise ValueError('Select --game-dir explicitly; detected installations:\n'+'\n'.join(str(x['game']) for x in matches))
        game=matches[0]['game']
    if not (game/'Lilith.exe').is_file() or not (game/'GameAssembly.dll').is_file():raise ValueError('This is not the assistant game directory (Lilith.exe and GameAssembly.dll required)')
    selected=matches[0] if len(matches)==1 else None
    steam=Path(args.steam_root).expanduser().resolve() if args.steam_root else selected['steam'] if selected else roots[0] if len(roots)==1 else None
    if steam is None or not (steam/'steamapps').is_dir():raise ValueError('Select the Steam root using --steam-root')
    compat=Path(args.compat_data).expanduser().resolve() if args.compat_data else selected['library']/'steamapps/compatdata'/APP_ID if selected else None
    if compat is None or not (compat/'pfx').is_dir():raise ValueError('Existing Steam Proton prefix required. Run the original game through Steam once, or specify --compat-data /library/steamapps/compatdata/'+APP_ID)
    sha=digest(game/'GameAssembly.dll')
    if sha!=KNOWN_GAME_SHA256 and not args.accept_untested_game:raise ValueError('Unverified game build. No files changed. Read docs/COMPATIBILITY.md; opt in with --accept-untested-game only if you accept the risks.')
    proton=choose_proton(args.proton,roots,installs)
    return {'schema':1,'app_id':APP_ID,'bridge_root':str(ROOT),'game_dir':str(game),'steam_root':str(steam),'compat_data':str(compat),'proton':str(proton),'game_sha256':sha,'verified_game_build':sha==KNOWN_GAME_SHA256,'steam_buildid':selected['buildid'] if selected else 'manual'}

def add_arguments(parser):
    parser.add_argument('--game-dir');parser.add_argument('--steam-root');parser.add_argument('--compat-data');parser.add_argument('--proton')
    parser.add_argument('--accept-untested-game',action='store_true',help='Explicitly allow an unverified game hash; may break after game updates')

def game_environment(config):
    env=os.environ.copy();env.update({'STEAM_COMPAT_DATA_PATH':config['compat_data'],'STEAM_COMPAT_CLIENT_INSTALL_PATH':config['steam_root'],'SteamAppId':APP_ID,'SteamGameId':APP_ID,'HOST_LC_ALL':'C.UTF-8','LC_ALL':'C.UTF-8','LC_CTYPE':'C.UTF-8'})
    existing=env.get('WINEDLLOVERRIDES','');parts=[p for p in existing.split(';') if p and p.split('=',1)[0].strip().lower()!='version'];env['WINEDLLOVERRIDES']=';'.join(parts+['version=n,b'])
    return env

def desktop_entry(root):
    value=str(Path(root).resolve()/'launch.sh');value=value.replace('%','%%').replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')
    return '[Desktop Entry]\nType=Application\nName=Lilith Linux Bridge\nComment=Unofficial native desktop bridge for The NOexistenceN of Lilith\nExec="'+value+'" start\nIcon=applications-games\nTerminal=false\nCategories=Utility;\nStartupNotify=false\nX-Lilith-Managed=true\n'
