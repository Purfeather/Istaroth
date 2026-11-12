"""Embed the unmodified selected PNG bytes, not a generated replacement."""
from pathlib import Path
import sys

root, out = map(Path, sys.argv[1:3])
data = (root/'assets/istaroth-mark.png').read_bytes()
assert data[:8] == b'\x89PNG\r\n\x1a\n' and 0 < len(data) <= 4*1024*1024
lines = [','.join(str(x) for x in data[i:i+24]) for i in range(0,len(data),24)]
(out/'resident_embedded_png.h').write_text('#pragma once\nstatic const unsigned char ResidentEmbeddedPng[]={\n'+',\n'.join(lines)+'\n};\n', encoding='ascii')
