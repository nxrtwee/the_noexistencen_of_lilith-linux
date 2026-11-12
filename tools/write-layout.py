#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
result=subprocess.check_output([sys.argv[1]],text=True);json.loads(result);Path(sys.argv[2]).write_text(result)
