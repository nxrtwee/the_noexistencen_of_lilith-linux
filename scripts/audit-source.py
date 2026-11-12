#!/usr/bin/env python3
"""Explicit source allowlist and privacy/artifact gate. Does not upload anything."""
import argparse,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TOP={'README.md','README.ru.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','CONTRIBUTING.md','SECURITY.md','VERSION','CMakeLists.txt','.gitignore','.gitattributes','.editorconfig','bridge_config.py','install.py','launch.py','launch.sh','run-game.py','build.sh','build-host.sh'}
DIRS={'src','tools','tests','docs','scripts','.github'}
EXT={'.c','.cpp','.h','.cfrag','.def','.js','.py','.md','.yml','.yaml','.txt'}
SKIP={'.git','build','dist','__pycache__','.cache','.venv'}

def source_files():
    files=[];errors=[]
    for p in sorted(ROOT.rglob('*')):
        r=p.relative_to(ROOT)
        if any(part in SKIP for part in r.parts):continue
        if p.is_symlink():errors.append('Symlink not allowed: '+r.as_posix());continue
        if not p.is_file():continue
        if len(r.parts)==1:allowed=p.name in TOP
        else:allowed=r.parts[0] in DIRS and p.suffix in EXT
        if not allowed:errors.append('Unexpected non-source file: '+r.as_posix());continue
        text=p.read_text(encoding='utf-8')
        if '\x00' in text:errors.append('Binary content in source file: '+r.as_posix())
        # Generic private home/mount paths, not a blacklist containing a real person's data.
        if re.search(r'/(?:home|mnt)/[^\s\"\'`<>]+',text):errors.append('Absolute personal/mount path: '+r.as_posix())
        if re.search(r'7656119[0-9]{10}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}',text):errors.append('Possible account identifier/secret: '+r.as_posix())
        files.append(p)
    if errors:raise ValueError('\n'.join(errors))
    return files

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--publication',action='store_true');a=p.parse_args()
    try:
        files=source_files()
        if a.publication and not (ROOT/'LICENSE').read_text().startswith('MIT License\n'):raise ValueError('Publication blocked: approved MIT license is missing or changed.')
    except (OSError,ValueError,UnicodeError) as e:print(str(e),file=sys.stderr);return 1
    print('PASS: '+str(len(files))+' allowlisted source/doc files; no private absolute paths, game binaries, dumps, saves, prefixes or obvious secrets.');return 0
if __name__=='__main__':raise SystemExit(main())
