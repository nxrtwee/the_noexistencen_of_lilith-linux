#!/usr/bin/env python3
"""Create local, explicitly allowlisted release candidates; never publish automatically."""
import hashlib,importlib.util,io,json,os,platform,re,subprocess,tarfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',ROOT/'scripts/audit-source.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
version=(ROOT/'VERSION').read_text().strip()
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?',version):raise SystemExit('Invalid VERSION')
files=audit.source_files();dist=ROOT/'dist';dist.mkdir(exist_ok=True)
def sha(data):return hashlib.sha256(data).hexdigest()
def tool(*args):
    try:return subprocess.check_output(args,text=True,stderr=subprocess.DEVNULL,timeout=10).splitlines()[0]
    except (OSError,subprocess.SubprocessError,IndexError):return 'unavailable'
provenance={'version':version,'candidate':True,'license':'MIT','code_license_pending':not (ROOT/'LICENSE').read_text().startswith('MIT License\n'),'architecture':platform.machine(),'libc':list(platform.libc_ver()),'native_compiler':tool('c++','--version'),'windows_compiler':tool('clang','--version'),'Qt6':tool('pkg-config','--modversion','Qt6Core'),'game_files_included':False,'tested_desktop':'KDE Plasma Wayland on CachyOS; not a universal Linux binary'}
provenance_bytes=(json.dumps(provenance,indent=2)+'\n').encode();(dist/'BUILD-PROVENANCE.json').write_bytes(provenance_bytes)
# Normalize archive paths/ownership/modes. Binary hashes remain specific to this build.
epoch=int(os.environ.get('SOURCE_DATE_EPOCH','0'))
def archive(name,payload):
    prefix=name.removesuffix('.tar.gz');manifest={str(p):sha(data) for p,data,_ in payload};payload=[*payload,('MANIFEST.sha256',(''.join(manifest[p]+'  '+p+'\n' for p in sorted(manifest))).encode(),0o644)]
    path=dist/name
    # gzip header timestamp is normalized too.
    import gzip
    with path.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=epoch) as gz,tarfile.open(fileobj=gz,mode='w') as tar:
        for rel,data,mode in sorted(payload):
            info=tarfile.TarInfo(prefix+'/'+str(rel));info.size=len(data);info.mode=mode;info.mtime=epoch;info.uid=info.gid=0;info.uname=info.gname='';tar.addfile(info,io.BytesIO(data))
    return path
source=[(p.relative_to(ROOT).as_posix(),p.read_bytes(),0o755 if p.suffix=='.sh' else 0o644) for p in files]
paths=[archive('lilith-linux-bridge-'+version+'-source.tar.gz',source)]
runtime_names={'README.md','README.ru.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','SECURITY.md','VERSION','bridge_config.py','install.py','launch.py','launch.sh','run-game.py','docs/INSTALLATION.md','docs/COMPATIBILITY.md','docs/RELEASING.md','src/host/kwin-windows.js'}
runtime_names.update(p.relative_to(ROOT).as_posix() for p in (ROOT/'docs').glob('*.md'))
runtime_names.update({'CONTRIBUTING.md'})
runtime=[(p,pth.read_bytes(),0o755 if p.endswith('.sh') else 0o644) for p in sorted(runtime_names) for pth in [ROOT/p]]
for name in ('version.dll','lilith-host','shm-layout.json'):
    path=ROOT/'build'/name
    if not path.is_file():raise SystemExit('Missing compiled payload: '+str(path))
    runtime.append(('build/'+name,path.read_bytes(),0o755 if name=='lilith-host' else 0o644))
runtime.append(('BUILD-PROVENANCE.json',provenance_bytes,0o644))
paths.append(archive('lilith-linux-bridge-'+version+'-linux-x86_64-arch-candidate.tar.gz',runtime))
(dist/'SHA256SUMS').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in paths))
for path in paths:print(path.name+' ('+str(path.stat().st_size)+' bytes)')
print('Candidate artifacts only. LICENSE pending: '+str(provenance['code_license_pending'])+'. No GitHub upload/tag/publication performed.')
