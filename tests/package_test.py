"""Exercise the real packager with owned dummy components; no game or network."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
names = ['Istaroth.exe','Istaroth.dll','time_core.dll','story_probe.dll','dialogue_actions.dll']
version = (root/'VERSION').read_text(encoding='utf8').strip()
digest = lambda data: hashlib.sha256(data).hexdigest()
with tempfile.TemporaryDirectory(prefix='istaroth-package-') as temporary:
    build = Path(temporary)/'build'
    build.mkdir()
    originals = {name: ('owned fixture '+name).encode() for name in names}
    for name, data in originals.items():
        (build/name).write_bytes(data)
    manifest = {'configuration': 'slim', 'version': version,
                'binaries': {name: digest(data) for name,data in originals.items()},
                'inputs': {'docs/USAGE.txt': digest((root/'docs/USAGE.txt').read_bytes())}}
    def save_manifest():
        (build/'build.json').write_text(json.dumps(manifest), encoding='utf8')
    def package(destination):
        return subprocess.run([sys.executable, '-B', str(root/'tools/package.py'), '--slim', str(build),
                               '--destination', str(destination)], capture_output=True)
    save_manifest()
    (build/'test-results.json').write_text('{"passed": true}', encoding='utf8')
    # Runtime private files must not enter an otherwise valid package.
    (build/'Istaroth.ini').write_text('GamePath=PRIVATE_SENTINEL', encoding='utf8')
    (build/'Istaroth-runtime.log').write_text('PRIVATE_SENTINEL', encoding='utf8')
    output = Path(temporary)/'valid'
    result = package(output)
    assert result.returncode == 0, result.stderr.decode('utf8', errors='replace')
    archive = output/f'Istaroth-v{version}-win64.zip'
    with zipfile.ZipFile(archive) as zipped:
        entries = {Path(name).name: zipped.read(name) for name in zipped.namelist()}
    assert set(entries) == set(names + ['LICENSES.txt','使用说明.txt'])
    assert all(entries[name] == data for name,data in originals.items())
    usage = entries['使用说明.txt']
    assert usage.startswith(b'\xef\xbb\xbf') and b'\r\n' in usage
    assert b'\n' not in usage.replace(b'\r\n', b'')
    assert usage.decode('utf-8-sig').replace('\r\n','\n') == (root/'docs/USAGE.txt').read_text(encoding='utf8')
    assert b'PRIVATE_SENTINEL' not in b''.join(entries.values())
    before = archive.read_bytes()
    assert package(output).returncode != 0 and archive.read_bytes() == before
    (build/'Istaroth.dll').write_bytes(b'tampered')
    assert package(Path(temporary)/'tampered').returncode != 0
    (build/'Istaroth.dll').write_bytes(originals['Istaroth.dll'])
    (build/'test-results.json').write_text('{"passed": false}', encoding='utf8')
    assert package(Path(temporary)/'failed-tests').returncode != 0
    (build/'test-results.json').write_text('{"passed": true}', encoding='utf8')
    manifest['inputs']['docs/USAGE.txt'] = '0'*64
    save_manifest()
    assert package(Path(temporary)/'stale-usage').returncode != 0
print(json.dumps({'passed': True, 'scope': 'seven-file allowlist, exact payloads, UTF-8 BOM/CRLF, private-file exclusion, no overwrite, tampered binary, failed tests and stale usage rejection'}))
