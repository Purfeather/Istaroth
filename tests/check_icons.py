"""Local WARP offscreen pixel comparison; needs the installed Chinese font."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
standard, slim = map(lambda x: Path(x).resolve(), sys.argv[1:3])
out = slim/'icon-checks'
out.mkdir(exist_ok=True)


def chunk(name, data):
    return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)


# A synthetic single-pixel fixture distinguishes an override from the default.
override = out/'override.png'
override.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(b'\x00\xff\x00\x00\xff'))+chunk(b'IEND',b''))
invalid = out/'invalid.png'
invalid.write_bytes(b'invalid test data')
results = []


def render(build, width, height, mode, name, icon):
    target = out/(name+'.png')
    result = subprocess.run([str(build/'tests/ui-snapshot.exe'),str(width),str(height),mode,str(target),str(icon)],
                            capture_output=True,text=True,timeout=25)
    assert result.returncode == 0, result.stderr
    assert 'icon_loaded=1' in result.stdout, result.stdout
    return hashlib.sha256(target.read_bytes()).hexdigest()


for width,height,mode in [(1920,1080,'waiting'),(1280,720,'hud-active')]:
    prefix = f'{width}-{mode}'
    expected = render(standard,width,height,mode,prefix+'-external',root/'assets/istaroth-mark.png')
    for case,path in [('absent',out/'absent.png'),('invalid',invalid),('directory',out)]:
        actual = render(slim,width,height,mode,prefix+'-'+case,path)
        assert actual == expected, f'Embedded default differs: {prefix}/{case}'
        results.append({'mode': prefix,'case': case,'pixel_output_identical': True})
    old_override = render(standard,width,height,mode,prefix+'-old-override',override)
    new_override = render(slim,width,height,mode,prefix+'-new-override',override)
    assert new_override == old_override and new_override != expected
    results.append({'mode': prefix,'case': 'external-override','pixel_output_identical': True})
(out/'results.json').write_text(json.dumps({'passed': True,'game_accessed': False,'comparisons': results},indent=2)+'\n',encoding='utf8')
print(f'{len(results)} icon comparisons passed; no window or desktop input.')
