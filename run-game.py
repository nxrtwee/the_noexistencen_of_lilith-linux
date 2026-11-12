#!/usr/bin/env python3
"""Run the user's existing Steam prefix through the explicitly configured Proton."""
import os,sys
from pathlib import Path
from bridge_config import load_config,game_environment
try:config=load_config()
except ValueError as e:raise SystemExit(str(e))
os.chdir(config['game_dir'])
os.execve(config['proton'],[config['proton'],'runinprefix','./Lilith.exe',*sys.argv[1:]],game_environment(config))
